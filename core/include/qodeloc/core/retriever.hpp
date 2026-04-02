#pragma once

#include <functional>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/hierarchy.hpp>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/storage.hpp>
#include <qodeloc/core/vector_store.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class Retriever final : public IModule {
public:
  struct Options {
    HierarchicalIndex::Options hierarchy;
    std::size_t related_symbol_limit{};
    std::size_t context_token_limit{};
  };

  struct SymbolContext {
    Indexer::IndexedSymbol symbol;
    double score{};
    std::vector<StoredSymbol> callers;
    std::vector<StoredSymbol> callees;
    std::string context;
    std::size_t token_count{};
  };

  struct Result {
    std::string query;
    Embedder::Embedding query_embedding;
    std::vector<HierarchicalIndex::ModuleHit> modules;
    std::vector<SymbolContext> symbols;
  };

  using QueryEmbeddingFn = std::function<Embedder::Embedding(std::string_view)>;
  using ModuleEmbeddingBatchFn = HierarchicalIndex::ModuleEmbeddingBatchFn;

  Retriever();
  explicit Retriever(Options options, QueryEmbeddingFn query_embedding = {});

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;

  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] const std::vector<Indexer::IndexedSymbol>& symbols() const noexcept;
  [[nodiscard]] const HierarchicalIndex& hierarchy() const noexcept;
  [[nodiscard]] const Storage* storage() const noexcept;

  void attach_storage(const Storage& storage) noexcept;
  void clear_storage() noexcept;
  void build(const std::vector<Indexer::IndexedSymbol>& symbols,
             const ModuleEmbeddingBatchFn& module_embedding_batch = {});
  void clear_corpus() noexcept;

  [[nodiscard]] Result retrieve(std::string_view query) const;
  [[nodiscard]] Result retrieve(const Embedder::Embedding& query_embedding,
                                std::string_view query = {}) const;

private:
  [[nodiscard]] Embedder::Embedding query_embedding(std::string_view query) const;
  [[nodiscard]] SymbolContext enrich_symbol(const Indexer::IndexedSymbol& symbol,
                                            double score) const;
  [[nodiscard]] std::string build_context(const SymbolContext& context) const;
  [[nodiscard]] bool append_line(std::string& context, std::size_t& tokens_used,
                                 std::string_view line) const;
  [[nodiscard]] static std::size_t count_tokens(std::string_view text);

  Options options_;
  QueryEmbeddingFn query_embedding_;
  Embedder embedder_;
  HierarchicalIndex hierarchy_;
  VectorStore vector_store_;
  std::vector<Indexer::IndexedSymbol> symbols_;
  const Storage* storage_{nullptr};
  bool corpus_ready_{false};
};

} // namespace qodeloc::core
