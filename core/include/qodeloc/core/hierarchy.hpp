#pragma once

#include <cstddef>
#include <functional>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/indexer.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class HierarchicalIndex final {
public:
  struct Options {
    std::size_t module_top_k{3};
    std::size_t symbol_top_k{5};
    std::size_t public_symbol_limit{12};
  };

  struct ModuleRecord {
    std::string module_name;
    std::string module_path;
    std::string summary;
    std::size_t public_symbol_count{};
    std::size_t header_count{};
    Embedder::Embedding embedding;
  };

  struct ModuleHit {
    ModuleRecord module;
    double score{};
  };

  struct SymbolHit {
    Indexer::IndexedSymbol symbol;
    double score{};
  };

  struct Result {
    std::vector<ModuleHit> modules;
    std::vector<SymbolHit> symbols;
  };

  using ModuleEmbeddingBatchFn = std::function<Embedder::Embeddings(std::span<const std::string>)>;

  HierarchicalIndex();
  explicit HierarchicalIndex(Options options);

  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] const std::vector<ModuleRecord>& modules() const noexcept;
  [[nodiscard]] const std::vector<Indexer::IndexedSymbol>& symbols() const noexcept;

  void build(const std::vector<Indexer::IndexedSymbol>& symbols,
             const ModuleEmbeddingBatchFn& module_embedding_batch);

  [[nodiscard]] Result search(const Embedder::Embedding& query_embedding) const;
  [[nodiscard]] std::vector<SymbolHit>
  search_flat(const Embedder::Embedding& query_embedding) const;

private:
  struct ModuleBucket {
    ModuleRecord record;
    std::vector<std::size_t> symbol_indexes;
  };

  struct ModuleProfile {
    std::string summary;
    std::size_t public_symbol_count{};
    std::size_t header_count{};
  };

  struct ModuleIdentityView {
    std::string_view module_name;
    std::string_view module_path;
  };

  struct ScoredModule {
    std::size_t index{};
    double score{};
  };

  struct ScoredSymbol {
    std::size_t index{};
    double score{};
  };

  [[nodiscard]] static bool is_header_file(std::string_view file_path);
  [[nodiscard]] static std::string short_name(std::string_view qualified_name);
  [[nodiscard]] static int symbol_rank(SymbolKind kind) noexcept;
  [[nodiscard]] static double cosine_similarity(std::span<const float> lhs,
                                                std::span<const float> rhs) noexcept;
  [[nodiscard]] static ModuleProfile build_module_profile(
      const ModuleIdentityView& module, std::span<const std::size_t> symbol_indexes,
      const std::vector<Indexer::IndexedSymbol>& symbols, std::size_t public_symbol_limit);
  [[nodiscard]] std::vector<ScoredModule>
  rank_modules(const Embedder::Embedding& query_embedding) const;
  [[nodiscard]] std::vector<ScoredSymbol>
  rank_symbols(const Embedder::Embedding& query_embedding,
               std::span<const std::size_t> symbol_indexes) const;

  Options options_;
  std::vector<Indexer::IndexedSymbol> symbols_;
  std::vector<ModuleRecord> module_records_;
  std::vector<ModuleBucket> modules_;
};

} // namespace qodeloc::core
