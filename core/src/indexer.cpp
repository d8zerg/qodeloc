#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <qodeloc/core/indexer.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace qodeloc::core {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] bool is_skippable_directory(std::string_view name) {
  static constexpr std::string_view kNames[] = {
      ".git", "build", "cmake-build-debug", "cmake-build-release", ".cache", "node_modules",
  };

  return std::find(std::begin(kNames), std::end(kNames), name) != std::end(kNames);
}

[[nodiscard]] std::string join_scopes(const std::vector<std::string>& parts, std::size_t count) {
  std::string result = parts.front();
  for (std::size_t index = 1; index < count; ++index) {
    result += "::";
    result += parts[index];
  }
  return result;
}

[[nodiscard]] std::string short_name(std::string_view qualified_name) {
  const auto position = qualified_name.rfind("::");
  if (position == std::string_view::npos) {
    return std::string(qualified_name);
  }

  return std::string(qualified_name.substr(position + 2));
}

[[nodiscard]] bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct ModuleIdentity {
  std::string name;
  std::string path;
};

struct ModuleResolutionInput {
  std::filesystem::path root_directory;
  std::filesystem::path file_path;
};

[[nodiscard]] bool has_cmake_lists(const std::filesystem::path& directory) {
  std::error_code ec;
  return std::filesystem::exists(directory / "CMakeLists.txt", ec);
}

[[nodiscard]] ModuleIdentity
resolve_module_identity(const ModuleResolutionInput& input,
                        std::unordered_map<std::string, ModuleIdentity>& cache) {
  const auto normalized_root = input.root_directory.lexically_normal();
  const auto normalized_parent = input.file_path.parent_path().lexically_normal();
  const auto cache_key = normalized_parent.generic_string();
  if (const auto cached = cache.find(cache_key); cached != cache.end()) {
    return cached->second;
  }

  std::filesystem::path module_root;
  for (auto current = normalized_parent; !current.empty();) {
    if (has_cmake_lists(current)) {
      module_root = current;
      break;
    }

    if (current == normalized_root) {
      break;
    }

    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }

  if (module_root.empty()) {
    std::error_code ec;
    const auto relative_parent = std::filesystem::relative(normalized_parent, normalized_root, ec);
    if (!ec && !relative_parent.empty() && relative_parent != ".") {
      module_root = normalized_root / *relative_parent.begin();
    } else {
      module_root = normalized_parent.empty() ? normalized_root : normalized_parent;
    }
  }

  std::error_code ec;
  const auto relative_module_root = std::filesystem::relative(module_root, normalized_root, ec);
  std::string module_path;
  if (!ec && !relative_module_root.empty() && relative_module_root != ".") {
    module_path = relative_module_root.lexically_normal().generic_string();
  } else if (!normalized_root.filename().generic_string().empty()) {
    module_path = normalized_root.filename().generic_string();
  } else {
    module_path = "root";
  }

  ModuleIdentity identity;
  identity.path = module_path;
  identity.name = module_path;
  cache.emplace(cache_key, identity);
  return identity;
}

[[nodiscard]] std::vector<std::string> split_qualified_name(std::string_view value) {
  std::vector<std::string> parts;
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto separator = value.find("::", offset);
    if (separator == std::string_view::npos) {
      parts.emplace_back(value.substr(offset));
      break;
    }

    parts.emplace_back(value.substr(offset, separator - offset));
    offset = separator + 2;
  }
  return parts;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::vector<std::string> candidate_names(std::string_view target_name,
                                                       std::string_view current_qualified_name) {
  std::vector<std::string> candidates;
  const auto target = std::string(target_name);

  if (target.find("::") != std::string::npos) {
    candidates.push_back(target);
    return candidates;
  }

  const auto parts = split_qualified_name(current_qualified_name);
  if (parts.size() >= 2) {
    for (std::size_t prefix_size = parts.size() - 1; prefix_size > 0; --prefix_size) {
      candidates.push_back(join_scopes(parts, prefix_size) + "::" + target);
    }
  }

  candidates.push_back(target);
  return candidates;
}

[[nodiscard]] std::string include_target_module(std::string_view include_path) {
  if (include_path.empty()) {
    return {};
  }

  const std::filesystem::path path{include_path};
  auto stem = path.stem().generic_string();
  if (!stem.empty()) {
    return stem;
  }

  return path.generic_string();
}

[[nodiscard]] std::string extract_snippet(const std::vector<std::string>& lines,
                                          std::uint32_t start_line, std::uint32_t end_line) {
  if (lines.empty() || start_line == 0 || end_line == 0 || start_line > end_line) {
    return {};
  }

  const auto first = std::max<std::size_t>(1, start_line);
  const auto last = std::min<std::size_t>(lines.size(), end_line);
  if (first > last) {
    return {};
  }

  std::string snippet;
  for (std::size_t line_index = first; line_index <= last; ++line_index) {
    if (line_index > first) {
      snippet.push_back('\n');
    }
    snippet += lines[line_index - 1];
  }

  return snippet;
}

[[nodiscard]] std::string eta_text(std::chrono::milliseconds elapsed, std::size_t processed,
                                   std::size_t total) {
  if (processed == 0 || total <= processed) {
    return "0ms";
  }

  const auto remaining = static_cast<double>(elapsed.count()) / static_cast<double>(processed) *
                         static_cast<double>(total - processed);
  std::ostringstream oss;
  oss << static_cast<std::int64_t>(remaining) << "ms";
  return oss.str();
}

[[nodiscard]] std::vector<std::string> read_file_lines(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("Failed to open source file: " + path.generic_string());
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

} // namespace

Indexer::Indexer() : Indexer(Options{}) {}

Indexer::Indexer(Options options, const std::filesystem::path& database_path,
                 EmbeddingBatchFn embedding_batch)
    : options_(std::move(options)), storage_(database_path), embedder_(),
      embedding_batch_(std::move(embedding_batch)) {
  if (options_.embedding_batch_size == 0) {
    throw std::invalid_argument("Indexer embedding batch size must be greater than zero");
  }
  if (!options_.root_directory.empty()) {
    options_.root_directory = options_.root_directory.lexically_normal();
  }
}

std::string_view Indexer::module_name() const noexcept {
  return "indexer";
}

bool Indexer::ready() const noexcept {
  if (!storage_.ready()) {
    return false;
  }

  if (options_.root_directory.empty()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::exists(options_.root_directory, ec) &&
         std::filesystem::is_directory(options_.root_directory, ec);
}

const Indexer::Options& Indexer::options() const noexcept {
  return options_;
}

Storage& Indexer::storage() noexcept {
  return storage_;
}

const Storage& Indexer::storage() const noexcept {
  return storage_;
}

Indexer::Result Indexer::index() {
  if (options_.root_directory.empty()) {
    throw std::runtime_error("Indexer root directory is not configured");
  }

  return index(options_.root_directory);
}

Indexer::Result Indexer::index(const std::filesystem::path& root_directory) {
  if (root_directory.empty()) {
    throw std::runtime_error("Indexer root directory is empty");
  }

  std::error_code ec;
  const auto normalized_root = root_directory.lexically_normal();
  if (!std::filesystem::exists(normalized_root, ec) ||
      !std::filesystem::is_directory(normalized_root, ec)) {
    throw std::runtime_error("Indexer root directory does not exist or is not a directory: " +
                             normalized_root.generic_string());
  }

  const auto started_at = Clock::now();
  const auto source_files =
      collect_source_files(normalized_root, options_.source_extensions, options_.recursive);

  Result result;
  result.stats.files_scanned = source_files.size();
  result.symbols.reserve(source_files.size() * 4);
  spdlog::info("Indexing {} source files under {}", source_files.size(),
               normalized_root.generic_string());

  std::vector<PendingSymbol> pending_symbols;
  pending_symbols.reserve(source_files.size() * 4);
  std::unordered_map<std::string, ModuleIdentity> module_cache;

  for (std::size_t file_index = 0; file_index < source_files.size(); ++file_index) {
    const auto& file_path = source_files[file_index];
    try {
      const auto lines = read_file_lines(file_path);
      const auto symbols = CppParser{}.parse_file(file_path);
      const auto module_identity =
          resolve_module_identity(ModuleResolutionInput{normalized_root, file_path}, module_cache);
      const auto& module_name = module_identity.name;
      const auto& module_path = module_identity.path;

      for (const auto& symbol : symbols) {
        PendingSymbol pending_symbol;
        pending_symbol.file_path = file_path;
        pending_symbol.module_name = module_name;
        pending_symbol.module_path = module_path;
        pending_symbol.symbol = StoredSymbol{
            file_path.generic_string(), module_name,      module_path,       symbol.kind,
            symbol.qualified_name,      symbol.signature, symbol.start_line, symbol.end_line};
        pending_symbol.dependencies = symbol.dependencies;
        pending_symbol.source_text = extract_snippet(lines, symbol.start_line, symbol.end_line);
        if (pending_symbol.source_text.empty()) {
          pending_symbol.source_text = symbol.signature;
        }
        if (pending_symbol.source_text.empty()) {
          pending_symbol.source_text = symbol.qualified_name;
        }
        pending_symbols.push_back(std::move(pending_symbol));
      }

      ++result.stats.files_indexed;
    } catch (const std::exception& error) {
      ++result.stats.parse_errors;
      spdlog::warn("Skipping {}: {}", file_path.generic_string(), error.what());
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_at);
    spdlog::info("[{}/{}] {} files={} symbols={} errors={} elapsed={} eta={}", file_index + 1,
                 source_files.size(), file_path.generic_string(), result.stats.files_indexed,
                 pending_symbols.size(), result.stats.parse_errors, elapsed.count(),
                 eta_text(elapsed, file_index + 1, source_files.size()));
  }

  std::vector<std::filesystem::path> files_to_refresh;
  files_to_refresh.reserve(source_files.size());
  std::unordered_set<std::string> seen_files;
  for (const auto& pending_symbol : pending_symbols) {
    const auto file_path = pending_symbol.file_path.generic_string();
    if (seen_files.insert(file_path).second) {
      files_to_refresh.push_back(pending_symbol.file_path);
    }
  }

  for (const auto& file_path : files_to_refresh) {
    storage_.graph().delete_file(file_path.generic_string());
  }

  std::vector<std::string> embedding_inputs;
  embedding_inputs.reserve(pending_symbols.size());
  for (const auto& pending_symbol : pending_symbols) {
    embedding_inputs.push_back(pending_symbol.source_text);
  }

  std::vector<Embedder::Embedding> embeddings;
  embeddings.reserve(pending_symbols.size());
  for (std::size_t offset = 0; offset < embedding_inputs.size();
       offset += options_.embedding_batch_size) {
    const auto count = std::min(options_.embedding_batch_size, embedding_inputs.size() - offset);
    std::vector<std::string> chunk;
    chunk.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      chunk.emplace_back(embedding_inputs[offset + index]);
    }

    auto batch = request_embeddings(chunk);
    if (batch.size() != count) {
      std::ostringstream oss;
      oss << "Embedding backend returned " << batch.size() << " vectors for " << count << " inputs";
      throw std::runtime_error(oss.str());
    }

    ++result.stats.embedding_batches;
    embeddings.insert(embeddings.end(), std::make_move_iterator(batch.begin()),
                      std::make_move_iterator(batch.end()));
  }

  result.symbols.reserve(pending_symbols.size());
  for (std::size_t index = 0; index < pending_symbols.size(); ++index) {
    const auto symbol_id = storage_.graph().write_symbol(pending_symbols[index].symbol);
    IndexedSymbol indexed_symbol;
    indexed_symbol.symbol_id = symbol_id;
    indexed_symbol.symbol = pending_symbols[index].symbol;
    indexed_symbol.source_text = std::move(pending_symbols[index].source_text);
    indexed_symbol.embedding = std::move(embeddings[index]);
    result.symbols.push_back(std::move(indexed_symbol));
  }

  for (std::size_t index = 0; index < pending_symbols.size(); ++index) {
    const auto source_id = result.symbols[index].symbol_id;
    const auto& pending_symbol = pending_symbols[index];

    for (const auto& include_path : pending_symbol.dependencies.includes) {
      storage_.graph().write_include(source_id, include_path, include_target_module(include_path));
    }

    for (const auto& base_class : pending_symbol.dependencies.base_classes) {
      const auto resolved =
          resolve_symbol_id(base_class, result.symbols, pending_symbol.symbol.qualified_name);
      if (resolved.has_value()) {
        storage_.graph().write_inheritance(source_id, *resolved);
      }
    }

    for (const auto& call : pending_symbol.dependencies.outgoing_calls) {
      const auto resolved =
          resolve_symbol_id(call, result.symbols, pending_symbol.symbol.qualified_name);
      if (resolved.has_value()) {
        storage_.graph().write_call(source_id, *resolved);
      }
    }
  }

  result.stats.symbols_indexed = result.symbols.size();
  result.stats.elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_at);

  spdlog::info("Indexed {} files, {} symbols, {} parse errors, {} embedding batches in {} ms",
               result.stats.files_indexed, result.stats.symbols_indexed, result.stats.parse_errors,
               result.stats.embedding_batches, result.stats.elapsed.count());

  return result;
}

bool Indexer::has_extension(const std::filesystem::path& path,
                            const std::vector<std::string>& extensions) {
  const auto extension = path.extension().generic_string();
  return std::any_of(extensions.begin(), extensions.end(),
                     [&extension](const std::string& allowed) { return extension == allowed; });
}

std::vector<std::filesystem::path>
Indexer::collect_source_files(const std::filesystem::path& root_directory,
                              const std::vector<std::string>& extensions, bool recursive) {
  std::vector<std::filesystem::path> files;
  const auto options = std::filesystem::directory_options::skip_permission_denied;

  if (recursive) {
    for (std::filesystem::recursive_directory_iterator it(root_directory, options), end; it != end;
         ++it) {
      const auto& entry = *it;
      if (entry.is_directory()) {
        if (is_skippable_directory(entry.path().filename().generic_string())) {
          it.disable_recursion_pending();
        }
        continue;
      }

      if (entry.is_regular_file() && has_extension(entry.path(), extensions)) {
        files.push_back(entry.path().lexically_normal());
      }
    }
  } else {
    for (const auto& entry : std::filesystem::directory_iterator(root_directory, options)) {
      if (entry.is_regular_file() && has_extension(entry.path(), extensions)) {
        files.push_back(entry.path().lexically_normal());
      }
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

std::optional<SymbolId>
Indexer::resolve_symbol_id(std::string_view target_name,
                           const std::vector<IndexedSymbol>& indexed_symbols,
                           std::string_view current_qualified_name) {
  const auto candidates = candidate_names(target_name, current_qualified_name);

  for (const auto& candidate : candidates) {
    for (const auto& symbol : indexed_symbols) {
      if (symbol.symbol.qualified_name == candidate) {
        return symbol.symbol_id;
      }
    }
  }

  const auto target_suffix = std::string("::") + std::string(target_name);
  for (const auto& symbol : indexed_symbols) {
    if (ends_with(symbol.symbol.qualified_name, target_suffix)) {
      return symbol.symbol_id;
    }
  }

  for (const auto& symbol : indexed_symbols) {
    if (short_name(symbol.symbol.qualified_name) == target_name) {
      return symbol.symbol_id;
    }
  }

  return std::nullopt;
}

Embedder::Embeddings Indexer::request_embeddings(std::span<const std::string> texts) const {
  if (embedding_batch_) {
    return embedding_batch_(texts);
  }

  return embedder_.embed_batch(texts);
}

} // namespace qodeloc::core
