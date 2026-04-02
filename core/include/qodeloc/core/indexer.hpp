#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/storage.hpp>
#include <qodeloc/core/vector_store.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class Indexer final : public IModule {
public:
  struct Options {
    std::filesystem::path root_directory;
    std::size_t embedding_batch_size{};
    bool recursive{};
    std::vector<std::string> source_extensions;
  };

  struct IndexedSymbol {
    SymbolId symbol_id{};
    StoredSymbol symbol;
    std::string source_text;
    Embedder::Embedding embedding;
  };

  struct Stats {
    std::size_t files_scanned{};
    std::size_t files_indexed{};
    std::size_t symbols_indexed{};
    std::size_t parse_errors{};
    std::size_t embedding_batches{};
    std::chrono::milliseconds elapsed{};
  };

  struct Result {
    Stats stats;
    std::vector<IndexedSymbol> symbols;
  };

  using EmbeddingBatchFn = std::function<Embedder::Embeddings(std::span<const std::string>)>;

  Indexer();
  explicit Indexer(Options options, const std::filesystem::path& database_path = {},
                   EmbeddingBatchFn embedding_batch = {});

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] const std::vector<IndexedSymbol>& symbols() const noexcept;
  [[nodiscard]] const Stats& last_stats() const noexcept;
  [[nodiscard]] std::chrono::system_clock::time_point last_indexed_at() const noexcept;
  [[nodiscard]] std::string_view last_operation() const noexcept;
  [[nodiscard]] Storage& storage() noexcept;
  [[nodiscard]] const Storage& storage() const noexcept;
  [[nodiscard]] Result index();
  [[nodiscard]] Result index(const std::filesystem::path& root_directory);
  [[nodiscard]] Result update(const std::vector<std::filesystem::path>& changed_files);
  [[nodiscard]] Result update_from_git(std::string_view base_ref = {});

private:
  struct PendingSymbol {
    std::filesystem::path file_path;
    std::string module_name;
    std::string module_path;
    StoredSymbol symbol;
    SymbolDependencies dependencies;
    std::string source_text;
  };

  [[nodiscard]] static bool has_extension(const std::filesystem::path& path,
                                          const std::vector<std::string>& extensions);
  [[nodiscard]] static std::vector<std::filesystem::path>
  collect_source_files(const std::filesystem::path& root_directory,
                       const std::vector<std::string>& extensions, bool recursive);
  [[nodiscard]] static std::vector<std::filesystem::path>
  normalize_changed_files(const std::filesystem::path& root_directory,
                          const std::vector<std::filesystem::path>& changed_files,
                          const std::vector<std::string>& extensions);
  [[nodiscard]] std::optional<SymbolId>
  resolve_symbol_id(std::string_view target_name, const std::vector<IndexedSymbol>& indexed_symbols,
                    std::string_view current_qualified_name) const;
  [[nodiscard]] Embedder::Embeddings request_embeddings(std::span<const std::string> texts) const;
  void record_result(const Result& result, std::string_view operation);
  [[nodiscard]] Result process_files(const std::vector<std::filesystem::path>& files_to_delete,
                                     const std::vector<std::filesystem::path>& files_to_parse);

  Options options_;
  Storage storage_;
  Embedder embedder_;
  VectorStore vector_store_;
  EmbeddingBatchFn embedding_batch_;
  std::vector<IndexedSymbol> symbols_;
  Stats last_stats_;
  std::chrono::system_clock::time_point last_indexed_at_{};
  std::string last_operation_;
};

} // namespace qodeloc::core
