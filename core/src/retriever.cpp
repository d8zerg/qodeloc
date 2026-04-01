#include <algorithm>
#include <cctype>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/retriever.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qodeloc::core {
namespace {

[[nodiscard]] std::string relation_line(std::string_view label, const StoredSymbol& symbol) {
  std::ostringstream oss;
  oss << label << " " << symbol.qualified_name;
  if (!symbol.signature.empty()) {
    oss << " :: " << symbol.signature;
  }
  if (!symbol.module_name.empty()) {
    oss << " [" << symbol.module_name << "]";
  }
  return oss.str();
}

} // namespace

Retriever::Retriever() : Retriever(Config::current().retriever_options()) {}

Retriever::Retriever(Options options, QueryEmbeddingFn query_embedding)
    : options_(std::move(options)), query_embedding_(std::move(query_embedding)) {
  if (options_.related_symbol_limit == 0) {
    throw std::invalid_argument("Retriever related symbol limit must be greater than zero");
  }
  if (options_.context_token_limit == 0) {
    throw std::invalid_argument("Retriever context token limit must be greater than zero");
  }
}

std::string_view Retriever::module_name() const noexcept {
  return "retriever";
}

bool Retriever::ready() const noexcept {
  return corpus_ready_ && storage_ != nullptr && (query_embedding_ || embedder_.ready());
}

const Retriever::Options& Retriever::options() const noexcept {
  return options_;
}

const std::vector<Indexer::IndexedSymbol>& Retriever::symbols() const noexcept {
  return symbols_;
}

const HierarchicalIndex& Retriever::hierarchy() const noexcept {
  return hierarchy_;
}

const Storage* Retriever::storage() const noexcept {
  return storage_;
}

void Retriever::attach_storage(const Storage& storage) noexcept {
  storage_ = &storage;
}

void Retriever::clear_storage() noexcept {
  storage_ = nullptr;
}

void Retriever::build(const std::vector<Indexer::IndexedSymbol>& symbols,
                      const ModuleEmbeddingBatchFn& module_embedding_batch) {
  symbols_ = symbols;
  hierarchy_ = HierarchicalIndex{options_.hierarchy};
  corpus_ready_ = !symbols_.empty();

  if (!corpus_ready_) {
    return;
  }

  if (module_embedding_batch) {
    hierarchy_.build(symbols_, module_embedding_batch);
    return;
  }

  hierarchy_.build(symbols_, [this](std::span<const std::string> summaries) {
    return embedder_.embed_batch(summaries);
  });
}

void Retriever::clear_corpus() noexcept {
  symbols_.clear();
  hierarchy_ = HierarchicalIndex{options_.hierarchy};
  corpus_ready_ = false;
}

Retriever::Result Retriever::retrieve(std::string_view query) const {
  if (!ready()) {
    throw std::runtime_error("Retriever is not ready");
  }

  return retrieve(query_embedding(query), query);
}

Retriever::Result Retriever::retrieve(const Embedder::Embedding& query_embedding,
                                      std::string_view query) const {
  if (!ready()) {
    throw std::runtime_error("Retriever is not ready");
  }

  const auto hierarchy_result = hierarchy_.search(query_embedding);
  Result result;
  result.query = std::string(query);
  result.query_embedding = query_embedding;
  result.modules = hierarchy_result.modules;
  result.symbols.reserve(hierarchy_result.symbols.size());

  for (const auto& hit : hierarchy_result.symbols) {
    result.symbols.push_back(enrich_symbol(hit));
  }

  return result;
}

Embedder::Embedding Retriever::query_embedding(std::string_view query) const {
  if (query_embedding_) {
    return query_embedding_(query);
  }

  return embedder_.embed(query);
}

Retriever::SymbolContext Retriever::enrich_symbol(const HierarchicalIndex::SymbolHit& hit) const {
  SymbolContext context;
  context.symbol = hit.symbol;
  context.score = hit.score;

  if (storage_ != nullptr) {
    context.callers = storage_->graph().callers_of(hit.symbol.symbol_id);
    context.callees = storage_->graph().callees_from(hit.symbol.symbol_id);
  }

  std::sort(context.callers.begin(), context.callers.end(),
            [](const StoredSymbol& lhs, const StoredSymbol& rhs) {
              if (lhs.qualified_name != rhs.qualified_name) {
                return lhs.qualified_name < rhs.qualified_name;
              }
              return lhs.file_path < rhs.file_path;
            });
  std::sort(context.callees.begin(), context.callees.end(),
            [](const StoredSymbol& lhs, const StoredSymbol& rhs) {
              if (lhs.qualified_name != rhs.qualified_name) {
                return lhs.qualified_name < rhs.qualified_name;
              }
              return lhs.file_path < rhs.file_path;
            });

  if (context.callers.size() > options_.related_symbol_limit) {
    context.callers.resize(options_.related_symbol_limit);
  }
  if (context.callees.size() > options_.related_symbol_limit) {
    context.callees.resize(options_.related_symbol_limit);
  }

  context.context = build_context(context);
  context.token_count = count_tokens(context.context);
  return context;
}

std::string Retriever::build_context(const SymbolContext& context) const {
  std::string result;
  std::size_t used_tokens = 0;

  const auto append = [&](std::string_view line) {
    if (!append_line(result, used_tokens, line)) {
      return false;
    }
    return true;
  };

  if (!append("Symbol: " + context.symbol.symbol.qualified_name)) {
    return result;
  }

  if (!context.symbol.symbol.module_name.empty()) {
    if (!append("Module: " + context.symbol.symbol.module_name)) {
      return result;
    }
  }

  if (!context.symbol.symbol.signature.empty()) {
    if (!append("Signature: " + context.symbol.symbol.signature)) {
      return result;
    }
  }

  if (!context.symbol.source_text.empty()) {
    if (!append("Source: " + context.symbol.source_text)) {
      return result;
    }
  }

  if (!context.callers.empty()) {
    if (!append("Callers:")) {
      return result;
    }
    for (const auto& caller : context.callers) {
      if (!append(relation_line("- ", caller))) {
        return result;
      }
    }
  }

  if (!context.callees.empty()) {
    if (!append("Callees:")) {
      return result;
    }
    for (const auto& callee : context.callees) {
      if (!append(relation_line("- ", callee))) {
        return result;
      }
    }
  }

  return result;
}

bool Retriever::append_line(std::string& context, std::size_t& tokens_used,
                            std::string_view line) const {
  const auto line_tokens = count_tokens(line);
  if (line_tokens == 0) {
    return true;
  }

  if (tokens_used + line_tokens > options_.context_token_limit) {
    return false;
  }

  if (!context.empty()) {
    context.push_back('\n');
  }
  context.append(line.begin(), line.end());
  tokens_used += line_tokens;
  return true;
}

std::size_t Retriever::count_tokens(std::string_view text) {
  std::size_t count = 0;
  bool in_token = false;
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      in_token = false;
      continue;
    }

    if (!in_token) {
      ++count;
      in_token = true;
    }
  }
  return count;
}

} // namespace qodeloc::core
