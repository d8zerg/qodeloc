#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <qodeloc/core/hierarchy.hpp>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace qodeloc::core {
namespace {

bool append_header(std::vector<std::string>& values, std::string value) {
  if (std::find(values.begin(), values.end(), value) != values.end()) {
    return false;
  }

  values.push_back(std::move(value));
  return true;
}

[[nodiscard]] std::string format_symbol_line(const Indexer::IndexedSymbol& symbol) {
  std::ostringstream oss;
  oss << symbol.symbol.kind << " " << symbol.symbol.qualified_name;
  if (!symbol.symbol.signature.empty()) {
    oss << " :: " << symbol.symbol.signature;
  }
  oss << " (" << symbol.symbol.file_path << ")";
  return oss.str();
}

} // namespace

HierarchicalIndex::HierarchicalIndex() : HierarchicalIndex(Options{}) {}

HierarchicalIndex::HierarchicalIndex(Options options) : options_(options) {
  if (options_.module_top_k == 0) {
    throw std::invalid_argument("HierarchicalIndex module_top_k must be greater than zero");
  }
  if (options_.symbol_top_k == 0) {
    throw std::invalid_argument("HierarchicalIndex symbol_top_k must be greater than zero");
  }
  if (options_.public_symbol_limit == 0) {
    throw std::invalid_argument("HierarchicalIndex public_symbol_limit must be greater than zero");
  }
}

const HierarchicalIndex::Options& HierarchicalIndex::options() const noexcept {
  return options_;
}

const std::vector<HierarchicalIndex::ModuleRecord>& HierarchicalIndex::modules() const noexcept {
  return module_records_;
}

const std::vector<Indexer::IndexedSymbol>& HierarchicalIndex::symbols() const noexcept {
  return symbols_;
}

void HierarchicalIndex::build(const std::vector<Indexer::IndexedSymbol>& symbols,
                              const ModuleEmbeddingBatchFn& module_embedding_batch) {
  if (!module_embedding_batch) {
    throw std::invalid_argument("HierarchicalIndex module embedding batch function is empty");
  }

  symbols_ = symbols;
  module_records_.clear();
  modules_.clear();

  if (symbols_.empty()) {
    return;
  }

  std::map<std::string, std::vector<std::size_t>> module_to_symbol_indexes;
  for (std::size_t index = 0; index < symbols_.size(); ++index) {
    const auto& symbol = symbols_[index].symbol;
    std::string module_key(symbol.module_name);
    module_key.push_back('\n');
    module_key += symbol.module_path;
    module_to_symbol_indexes[module_key].push_back(index);
  }

  modules_.reserve(module_to_symbol_indexes.size());
  std::vector<std::string> module_summaries;
  module_summaries.reserve(module_to_symbol_indexes.size());

  for (const auto& [module_key, symbol_indexes] : module_to_symbol_indexes) {
    (void)module_key;
    const auto first_index = symbol_indexes.front();
    const auto& first_symbol = symbols_[first_index].symbol;
    ModuleBucket bucket;
    bucket.record.module_name = first_symbol.module_name;
    bucket.record.module_path = first_symbol.module_path;

    const auto profile = build_module_profile(
        HierarchicalIndex::ModuleIdentityView{bucket.record.module_name, bucket.record.module_path},
        symbol_indexes, symbols_, options_.public_symbol_limit);
    bucket.record.summary = profile.summary;
    bucket.record.public_symbol_count = profile.public_symbol_count;
    bucket.record.header_count = profile.header_count;
    bucket.symbol_indexes = symbol_indexes;

    module_summaries.push_back(bucket.record.summary);
    modules_.push_back(std::move(bucket));
  }

  const auto embeddings = module_embedding_batch(module_summaries);
  if (embeddings.size() != modules_.size()) {
    throw std::runtime_error("HierarchicalIndex module embedding backend returned " +
                             std::to_string(embeddings.size()) + " vectors for " +
                             std::to_string(modules_.size()) + " modules");
  }

  for (std::size_t index = 0; index < modules_.size(); ++index) {
    modules_[index].record.embedding = embeddings[index];
    module_records_.push_back(modules_[index].record);
  }
}

HierarchicalIndex::Result
HierarchicalIndex::search(const Embedder::Embedding& query_embedding) const {
  Result result;
  const auto scored_modules = rank_modules(query_embedding);
  result.modules.reserve(scored_modules.size());

  std::unordered_set<std::size_t> candidate_indexes;
  for (const auto& scored_module : scored_modules) {
    const auto& bucket = modules_[scored_module.index];
    result.modules.push_back(ModuleHit{bucket.record, scored_module.score});
    candidate_indexes.insert(bucket.symbol_indexes.begin(), bucket.symbol_indexes.end());
  }

  std::vector<std::size_t> candidate_vector;
  candidate_vector.reserve(candidate_indexes.size());
  candidate_vector.insert(candidate_vector.end(), candidate_indexes.begin(),
                          candidate_indexes.end());

  const auto scored_symbols = rank_symbols(query_embedding, candidate_vector);
  result.symbols.reserve(scored_symbols.size());
  for (const auto& scored_symbol : scored_symbols) {
    result.symbols.push_back(SymbolHit{symbols_[scored_symbol.index], scored_symbol.score});
  }

  return result;
}

std::vector<HierarchicalIndex::SymbolHit>
HierarchicalIndex::search_flat(const Embedder::Embedding& query_embedding) const {
  std::vector<std::size_t> all_indexes(symbols_.size());
  std::iota(all_indexes.begin(), all_indexes.end(), 0U);

  const auto scored_symbols = rank_symbols(query_embedding, all_indexes);
  std::vector<SymbolHit> result;
  result.reserve(scored_symbols.size());
  for (const auto& scored_symbol : scored_symbols) {
    result.push_back(SymbolHit{symbols_[scored_symbol.index], scored_symbol.score});
  }
  return result;
}

bool HierarchicalIndex::is_header_file(std::string_view file_path) {
  const std::filesystem::path path{file_path};
  const auto extension = path.extension().generic_string();
  return extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx";
}

std::string HierarchicalIndex::short_name(std::string_view qualified_name) {
  const auto position = qualified_name.rfind("::");
  if (position == std::string_view::npos) {
    return std::string(qualified_name);
  }

  return std::string(qualified_name.substr(position + 2));
}

int HierarchicalIndex::symbol_rank(SymbolKind kind) noexcept {
  switch (kind) {
  case SymbolKind::Namespace:
    return 0;
  case SymbolKind::Class:
    return 1;
  case SymbolKind::Enum:
    return 2;
  case SymbolKind::Template:
    return 3;
  case SymbolKind::Function:
    return 4;
  case SymbolKind::Method:
    return 5;
  }

  return 6;
}

double HierarchicalIndex::cosine_similarity(std::span<const float> lhs,
                                            std::span<const float> rhs) noexcept {
  if (lhs.empty() || rhs.empty() || lhs.size() != rhs.size()) {
    return 0.0;
  }

  double dot = 0.0;
  double lhs_norm = 0.0;
  double rhs_norm = 0.0;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const double left = static_cast<double>(lhs[index]);
    const double right = static_cast<double>(rhs[index]);
    dot += left * right;
    lhs_norm += left * left;
    rhs_norm += right * right;
  }

  if (lhs_norm <= 0.0 || rhs_norm <= 0.0) {
    return 0.0;
  }

  return dot / std::sqrt(lhs_norm * rhs_norm);
}

HierarchicalIndex::ModuleProfile
HierarchicalIndex::build_module_profile(const HierarchicalIndex::ModuleIdentityView& module,
                                        std::span<const std::size_t> symbol_indexes,
                                        const std::vector<Indexer::IndexedSymbol>& symbols,
                                        std::size_t public_symbol_limit) {
  std::vector<std::string> header_files;
  std::vector<std::size_t> public_candidates;
  std::vector<std::size_t> fallback_candidates;
  header_files.reserve(symbol_indexes.size());
  public_candidates.reserve(std::min(symbol_indexes.size(), public_symbol_limit));
  fallback_candidates.reserve(symbol_indexes.size());

  for (const auto index : symbol_indexes) {
    const auto& indexed_symbol = symbols[index];
    const auto& file_path = indexed_symbol.symbol.file_path;
    if (is_header_file(file_path)) {
      append_header(header_files, file_path);
      public_candidates.push_back(index);
    } else {
      fallback_candidates.push_back(index);
    }
  }

  const auto candidate_sort = [&symbols](std::size_t lhs, std::size_t rhs) {
    const auto& left = symbols[lhs];
    const auto& right = symbols[rhs];
    const auto left_header = is_header_file(left.symbol.file_path);
    const auto right_header = is_header_file(right.symbol.file_path);
    if (left_header != right_header) {
      return left_header && !right_header;
    }

    const auto left_rank = symbol_rank(left.symbol.kind);
    const auto right_rank = symbol_rank(right.symbol.kind);
    if (left_rank != right_rank) {
      return left_rank < right_rank;
    }

    if (left.symbol.file_path != right.symbol.file_path) {
      return left.symbol.file_path < right.symbol.file_path;
    }

    if (left.symbol.start_line != right.symbol.start_line) {
      return left.symbol.start_line < right.symbol.start_line;
    }

    return left.symbol.qualified_name < right.symbol.qualified_name;
  };

  std::sort(public_candidates.begin(), public_candidates.end(), candidate_sort);
  std::sort(fallback_candidates.begin(), fallback_candidates.end(), candidate_sort);

  std::vector<std::size_t> selected;
  selected.reserve(public_symbol_limit);
  for (const auto index : public_candidates) {
    selected.push_back(index);
    if (selected.size() >= public_symbol_limit) {
      break;
    }
  }
  for (const auto index : fallback_candidates) {
    if (selected.size() >= public_symbol_limit) {
      break;
    }
    selected.push_back(index);
  }

  if (selected.empty()) {
    const auto count = std::min(symbol_indexes.size(), public_symbol_limit);
    selected.assign(symbol_indexes.begin(),
                    symbol_indexes.begin() +
                        static_cast<std::vector<std::size_t>::difference_type>(count));
  }

  std::ostringstream summary;
  summary << "module " << module.module_name << '\n';
  summary << "path " << module.module_path << '\n';
  summary << "headers " << header_files.size() << '\n';
  summary << "public symbols " << selected.size() << '\n';
  summary << "selected symbols:\n";
  for (const auto index : selected) {
    summary << "- " << format_symbol_line(symbols[index]) << '\n';
  }

  ModuleProfile profile;
  profile.summary = summary.str();
  profile.public_symbol_count = selected.size();
  profile.header_count = header_files.size();
  return profile;
}

std::vector<HierarchicalIndex::ScoredModule>
HierarchicalIndex::rank_modules(const Embedder::Embedding& query_embedding) const {
  std::vector<ScoredModule> scores;
  scores.reserve(modules_.size());
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    scores.push_back(
        ScoredModule{index, cosine_similarity(query_embedding, modules_[index].record.embedding)});
  }

  std::sort(scores.begin(), scores.end(), [this](const ScoredModule& lhs, const ScoredModule& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }

    const auto& left = modules_[lhs.index].record;
    const auto& right = modules_[rhs.index].record;
    if (left.module_name != right.module_name) {
      return left.module_name < right.module_name;
    }

    return left.module_path < right.module_path;
  });

  if (scores.size() > options_.module_top_k) {
    scores.resize(options_.module_top_k);
  }

  return scores;
}

std::vector<HierarchicalIndex::ScoredSymbol>
HierarchicalIndex::rank_symbols(const Embedder::Embedding& query_embedding,
                                std::span<const std::size_t> symbol_indexes) const {
  std::vector<ScoredSymbol> scores;
  scores.reserve(symbol_indexes.size());
  for (const auto index : symbol_indexes) {
    scores.push_back(
        ScoredSymbol{index, cosine_similarity(query_embedding, symbols_[index].embedding)});
  }

  std::sort(scores.begin(), scores.end(), [this](const ScoredSymbol& lhs, const ScoredSymbol& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }

    const auto& left = symbols_[lhs.index].symbol;
    const auto& right = symbols_[rhs.index].symbol;
    if (left.module_name != right.module_name) {
      return left.module_name < right.module_name;
    }

    if (left.qualified_name != right.qualified_name) {
      return left.qualified_name < right.qualified_name;
    }

    if (left.file_path != right.file_path) {
      return left.file_path < right.file_path;
    }

    return left.start_line < right.start_line;
  });

  if (scores.size() > options_.symbol_top_k) {
    scores.resize(options_.symbol_top_k);
  }

  return scores;
}

} // namespace qodeloc::core
