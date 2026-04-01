#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <qodeloc/core/config.hpp>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qodeloc::core {
namespace {

[[nodiscard]] std::string trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }

  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(begin, end - begin + 1)};
}

[[nodiscard]] std::string lower_copy(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

[[nodiscard]] bool parse_bool(std::string_view value, std::string_view key) {
  const auto lowered = lower_copy(value);
  if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
    return true;
  }
  if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
    return false;
  }

  std::ostringstream oss;
  oss << "Invalid boolean value for " << key << ": " << value;
  throw std::runtime_error(oss.str());
}

[[nodiscard]] std::size_t parse_size_t(std::string_view value, std::string_view key) {
  try {
    const auto parsed = std::stoull(std::string(value));
    if (parsed > static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)())) {
      throw std::out_of_range("size_t overflow");
    }
    return static_cast<std::size_t>(parsed);
  } catch (const std::exception&) {
    std::ostringstream oss;
    oss << "Invalid size_t value for " << key << ": " << value;
    throw std::runtime_error(oss.str());
  }
}

[[nodiscard]] std::uint16_t parse_u16(std::string_view value, std::string_view key) {
  const auto parsed = parse_size_t(value, key);
  if (parsed > static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())) {
    std::ostringstream oss;
    oss << "Port value out of range for " << key << ": " << value;
    throw std::runtime_error(oss.str());
  }
  return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::chrono::milliseconds parse_milliseconds(std::string_view value,
                                                           std::string_view key) {
  try {
    const auto parsed = std::stoll(std::string(value));
    if (parsed < 0) {
      throw std::out_of_range("negative duration");
    }
    return std::chrono::milliseconds{parsed};
  } catch (const std::exception&) {
    std::ostringstream oss;
    oss << "Invalid millisecond value for " << key << ": " << value;
    throw std::runtime_error(oss.str());
  }
}

[[nodiscard]] std::vector<std::string> parse_extensions(std::string_view value) {
  std::vector<std::string> extensions;
  std::string token;
  std::istringstream input{std::string(value)};
  while (std::getline(input, token, ',')) {
    const auto trimmed = trim(token);
    if (!trimmed.empty()) {
      extensions.push_back(trimmed);
    }
  }
  return extensions;
}

[[nodiscard]] std::filesystem::path resolve_path(const std::filesystem::path& base_directory,
                                                 std::string_view value) {
  if (value.empty()) {
    return {};
  }

  std::filesystem::path path{value};
  if (path.is_absolute()) {
    return path.lexically_normal();
  }

  if (base_directory.empty()) {
    return path.lexically_normal();
  }

  return (base_directory / path).lexically_normal();
}

[[nodiscard]] std::unordered_map<std::string, std::string>
parse_env_file(const std::filesystem::path& env_file) {
  std::unordered_map<std::string, std::string> values;
  std::ifstream input(env_file);
  if (!input) {
    std::ostringstream oss;
    oss << "Failed to open config env file: " << env_file.generic_string();
    throw std::runtime_error(oss.str());
  }

  std::string line;
  while (std::getline(input, line)) {
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    std::string content = trimmed;
    if (content.starts_with("export ")) {
      content.erase(0, 7);
    }

    const auto equal = content.find('=');
    if (equal == std::string::npos) {
      continue;
    }

    const auto key = trim(content.substr(0, equal));
    auto value = trim(content.substr(equal + 1));
    if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                           (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }

    values.emplace(key, std::move(value));
  }

  return values;
}

[[nodiscard]] std::filesystem::path find_env_file(const std::filesystem::path& start) {
  std::error_code ec;
  for (auto current = std::filesystem::absolute(start, ec); !current.empty();) {
    const auto candidate = current / ".env";
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.lexically_normal();
    }

    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return {};
}

[[nodiscard]] std::filesystem::path find_workspace_root(const std::filesystem::path& start) {
  std::error_code ec;
  for (auto current = std::filesystem::absolute(start, ec); !current.empty();) {
    if (std::filesystem::exists(current / "VERSION", ec)) {
      return current.lexically_normal();
    }

    const auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return start.lexically_normal();
}

[[nodiscard]] std::string lookup_value(const std::unordered_map<std::string, std::string>& values,
                                       std::string_view key, const char* fallback) {
  if (const char* env = std::getenv(std::string(key).c_str()); env != nullptr) {
    return std::string(env);
  }

  if (const auto it = values.find(std::string(key)); it != values.end()) {
    return it->second;
  }

  return std::string(fallback);
}

} // namespace

Config Config::load(const std::filesystem::path& env_file) {
  Config config;

  std::filesystem::path resolved_env_file = env_file;
  if (!resolved_env_file.empty() && resolved_env_file.is_relative()) {
    resolved_env_file = std::filesystem::absolute(resolved_env_file);
  }

  if (!resolved_env_file.empty()) {
    if (!std::filesystem::exists(resolved_env_file)) {
      std::ostringstream oss;
      oss << "Config env file does not exist: " << resolved_env_file.generic_string();
      throw std::runtime_error(oss.str());
    }
  } else {
    resolved_env_file = find_env_file(std::filesystem::current_path());
  }

  config.env_file_path_ = resolved_env_file.lexically_normal();
  config.root_directory_ = resolved_env_file.empty()
                               ? find_workspace_root(std::filesystem::current_path())
                               : resolved_env_file.parent_path().lexically_normal();

  const auto file_values = resolved_env_file.empty()
                               ? std::unordered_map<std::string, std::string>{}
                               : parse_env_file(resolved_env_file);

  config.embedder_options_.host = lookup_value(file_values, "QODELOC_EMBEDDER_HOST", "127.0.0.1");
  config.embedder_options_.port = parse_u16(
      lookup_value(file_values, "QODELOC_EMBEDDER_PORT", "8080"), "QODELOC_EMBEDDER_PORT");
  config.embedder_options_.api_path =
      lookup_value(file_values, "QODELOC_EMBEDDER_API_PATH", "/v1/embeddings");
  config.embedder_options_.model =
      lookup_value(file_values, "QODELOC_EMBEDDER_MODEL", "qodeloc-embedding");
  config.embedder_options_.batch_size = parse_size_t(
      lookup_value(file_values, "QODELOC_EMBEDDER_BATCH_SIZE", "8"), "QODELOC_EMBEDDER_BATCH_SIZE");
  config.embedder_options_.timeout =
      parse_milliseconds(lookup_value(file_values, "QODELOC_EMBEDDER_TIMEOUT_MS", "30000"),
                         "QODELOC_EMBEDDER_TIMEOUT_MS");

  config.llm_options_.host = lookup_value(file_values, "QODELOC_LLM_HOST", "127.0.0.1");
  config.llm_options_.port =
      parse_u16(lookup_value(file_values, "QODELOC_LLM_PORT", "4000"), "QODELOC_LLM_PORT");
  config.llm_options_.api_path =
      lookup_value(file_values, "QODELOC_LLM_API_PATH", "/v1/chat/completions");
  config.llm_options_.model = lookup_value(file_values, "QODELOC_LLM_MODEL", "qodeloc-local");
  config.llm_options_.api_key = lookup_value(file_values, "QODELOC_LLM_API_KEY", "sk-qodeloc-dev");
  config.llm_options_.timeout = parse_milliseconds(
      lookup_value(file_values, "QODELOC_LLM_TIMEOUT_MS", "30000"), "QODELOC_LLM_TIMEOUT_MS");
  config.llm_options_.max_retries = parse_size_t(
      lookup_value(file_values, "QODELOC_LLM_MAX_RETRIES", "3"), "QODELOC_LLM_MAX_RETRIES");
  config.llm_options_.initial_backoff =
      parse_milliseconds(lookup_value(file_values, "QODELOC_LLM_INITIAL_BACKOFF_MS", "150"),
                         "QODELOC_LLM_INITIAL_BACKOFF_MS");
  config.llm_options_.max_backoff =
      parse_milliseconds(lookup_value(file_values, "QODELOC_LLM_MAX_BACKOFF_MS", "2000"),
                         "QODELOC_LLM_MAX_BACKOFF_MS");

  config.prompt_builder_options_.templates_directory = resolve_path(
      config.root_directory_, lookup_value(file_values, "QODELOC_PROMPTS_DIR", "prompts"));
  config.prompt_builder_options_.context_token_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_PROMPT_CONTEXT_TOKEN_LIMIT", "3072"),
                   "QODELOC_PROMPT_CONTEXT_TOKEN_LIMIT");
  config.prompt_builder_options_.module_limit = parse_size_t(
      lookup_value(file_values, "QODELOC_PROMPT_MODULE_LIMIT", "4"), "QODELOC_PROMPT_MODULE_LIMIT");
  config.prompt_builder_options_.symbol_limit = parse_size_t(
      lookup_value(file_values, "QODELOC_PROMPT_SYMBOL_LIMIT", "8"), "QODELOC_PROMPT_SYMBOL_LIMIT");
  config.prompt_builder_options_.local_file_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_PROMPT_LOCAL_FILE_LIMIT", "3"),
                   "QODELOC_PROMPT_LOCAL_FILE_LIMIT");
  config.prompt_builder_options_.module_token_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_PROMPT_MODULE_TOKEN_LIMIT", "256"),
                   "QODELOC_PROMPT_MODULE_TOKEN_LIMIT");
  config.prompt_builder_options_.symbol_token_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_PROMPT_SYMBOL_TOKEN_LIMIT", "1024"),
                   "QODELOC_PROMPT_SYMBOL_TOKEN_LIMIT");
  config.prompt_builder_options_.local_file_token_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_PROMPT_LOCAL_FILE_TOKEN_LIMIT", "768"),
                   "QODELOC_PROMPT_LOCAL_FILE_TOKEN_LIMIT");

  config.hierarchy_options_.module_top_k =
      parse_size_t(lookup_value(file_values, "QODELOC_HIERARCHY_MODULE_TOP_K", "3"),
                   "QODELOC_HIERARCHY_MODULE_TOP_K");
  config.hierarchy_options_.symbol_top_k =
      parse_size_t(lookup_value(file_values, "QODELOC_HIERARCHY_SYMBOL_TOP_K", "5"),
                   "QODELOC_HIERARCHY_SYMBOL_TOP_K");
  config.hierarchy_options_.public_symbol_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_HIERARCHY_PUBLIC_SYMBOL_LIMIT", "12"),
                   "QODELOC_HIERARCHY_PUBLIC_SYMBOL_LIMIT");

  config.retriever_options_.hierarchy = config.hierarchy_options_;
  config.retriever_options_.related_symbol_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_RETRIEVER_RELATED_SYMBOL_LIMIT", "4"),
                   "QODELOC_RETRIEVER_RELATED_SYMBOL_LIMIT");
  config.retriever_options_.context_token_limit =
      parse_size_t(lookup_value(file_values, "QODELOC_RETRIEVER_CONTEXT_TOKEN_LIMIT", "256"),
                   "QODELOC_RETRIEVER_CONTEXT_TOKEN_LIMIT");

  config.api_options_.host = lookup_value(file_values, "QODELOC_API_HOST", "127.0.0.1");
  config.api_options_.port =
      parse_u16(lookup_value(file_values, "QODELOC_API_PORT", "3100"), "QODELOC_API_PORT");
  config.api_options_.max_body_bytes =
      parse_size_t(lookup_value(file_values, "QODELOC_API_MAX_BODY_BYTES", "1048576"),
                   "QODELOC_API_MAX_BODY_BYTES");
  config.api_options_.request_timeout = parse_milliseconds(
      lookup_value(file_values, "QODELOC_API_TIMEOUT_MS", "30000"), "QODELOC_API_TIMEOUT_MS");

  config.indexer_options_.embedding_batch_size =
      parse_size_t(lookup_value(file_values, "QODELOC_INDEXER_EMBEDDING_BATCH_SIZE", "8"),
                   "QODELOC_INDEXER_EMBEDDING_BATCH_SIZE");
  config.indexer_options_.recursive = parse_bool(
      lookup_value(file_values, "QODELOC_INDEXER_RECURSIVE", "true"), "QODELOC_INDEXER_RECURSIVE");
  config.indexer_options_.source_extensions = parse_extensions(
      lookup_value(file_values, "QODELOC_INDEXER_SOURCE_EXTENSIONS", ".cpp,.cxx,.cc,.h,.hpp,.hxx"));
  if (config.indexer_options_.source_extensions.empty()) {
    config.indexer_options_.source_extensions = {".cpp", ".cxx", ".cc", ".h", ".hpp", ".hxx"};
  }

  config.storage_database_path_ = resolve_path(
      config.root_directory_, lookup_value(file_values, "QODELOC_STORAGE_DB_PATH", ""));
  config.git_base_ref_ = lookup_value(file_values, "QODELOC_GIT_BASE_REF", "HEAD~1");
  if (trim(config.git_base_ref_).empty()) {
    config.git_base_ref_ = "HEAD~1";
  }

  return config;
}

const Config& Config::current() {
  static const Config config = Config::load();
  return config;
}

const std::filesystem::path& Config::env_file_path() const noexcept {
  return env_file_path_;
}

const std::filesystem::path& Config::root_directory() const noexcept {
  return root_directory_;
}

Embedder::Options Config::embedder_options() const {
  return embedder_options_;
}

LlmClient::Options Config::llm_options() const {
  return llm_options_;
}

PromptBuilder::Options Config::prompt_builder_options() const {
  return prompt_builder_options_;
}

HierarchicalIndex::Options Config::hierarchy_options() const {
  return hierarchy_options_;
}

Retriever::Options Config::retriever_options() const {
  return retriever_options_;
}

ApiServer::Options Config::api_options() const {
  return api_options_;
}

Indexer::Options Config::indexer_options(const std::filesystem::path& root_directory) const {
  auto options = indexer_options_;
  options.root_directory = root_directory;
  return options;
}

GitWatcher::Options
Config::git_watcher_options(const std::filesystem::path& repository_root) const {
  GitWatcher::Options options;
  options.repository_root = repository_root;
  options.base_ref = git_base_ref_;
  return options;
}

std::filesystem::path Config::storage_database_path() const {
  return storage_database_path_;
}

std::string Config::git_base_ref() const {
  return git_base_ref_;
}

} // namespace qodeloc::core
