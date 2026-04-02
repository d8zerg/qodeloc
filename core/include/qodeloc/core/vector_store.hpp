#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <boost/beast/http.hpp>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/storage.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class VectorStore final : public IModule {
public:
  struct Options {
    bool enabled{};
    std::string host;
    std::uint16_t port{};
    std::string collection;
    std::size_t vector_size{};
    std::chrono::milliseconds timeout{};
    std::string api_key;
  };

  struct Record {
    SymbolId symbol_id{};
    StoredSymbol symbol;
    std::string source_text;
    Embedder::Embedding embedding;
  };

  struct SearchHit {
    Record record;
    double score{};
  };

  VectorStore();
  explicit VectorStore(Options options);

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] const Options& options() const noexcept;

  void ensure_collection() const;
  void upsert_record(const Record& record) const;
  void upsert_records(std::span<const Record> records) const;
  void delete_file(std::string_view file_path) const;
  void delete_files(std::span<const std::filesystem::path> file_paths) const;

  [[nodiscard]] std::vector<SearchHit> search(const Embedder::Embedding& query_embedding,
                                              std::size_t limit) const;

private:
  [[nodiscard]] static std::string make_path(std::string_view suffix);
  [[nodiscard]] static std::string join_host_port(std::string_view host, std::uint16_t port);
  [[nodiscard]] nlohmann::json request_json(boost::beast::http::verb method,
                                            std::string_view path,
                                            const nlohmann::json* body = nullptr) const;
  [[nodiscard]] SearchHit parse_search_hit(const nlohmann::json& json) const;
  [[nodiscard]] static Record parse_record(const nlohmann::json& payload, SymbolId fallback_id);
  void ensure_collection_impl() const;

  Options options_;
  mutable std::once_flag collection_once_;
};

} // namespace qodeloc::core
