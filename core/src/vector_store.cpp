#include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/vector_store.hpp>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

void close_stream(beast::tcp_stream& connection) noexcept {
  boost::system::error_code ec;
  const auto shutdown_result = connection.socket().shutdown(tcp::socket::shutdown_both, ec);
  if (shutdown_result) {
    ec = shutdown_result;
  }

  const auto close_result = connection.socket().close(ec);
  if (close_result) {
    ec = close_result;
  }
}

[[nodiscard]] std::string build_file_filter(std::string_view file_path) {
  nlohmann::json filter;
  filter["must"] = nlohmann::json::array();
  filter["must"].push_back(
      {{"key", "file_path"}, {"match", {{"value", std::string(file_path)}}}});
  return filter.dump();
}

} // namespace

VectorStore::VectorStore() : VectorStore(Config::current().vector_store_options()) {}

VectorStore::VectorStore(Options options) : options_(std::move(options)) {
  if (options_.enabled) {
    if (options_.host.empty()) {
      throw std::invalid_argument("VectorStore host must not be empty when enabled");
    }
    if (options_.port == 0) {
      throw std::invalid_argument("VectorStore port must be greater than zero when enabled");
    }
    if (options_.collection.empty()) {
      throw std::invalid_argument("VectorStore collection must not be empty when enabled");
    }
    if (options_.vector_size == 0) {
      throw std::invalid_argument("VectorStore vector size must be greater than zero when enabled");
    }
    if (options_.timeout.count() <= 0) {
      throw std::invalid_argument("VectorStore timeout must be positive");
    }
  }
}

std::string_view VectorStore::module_name() const noexcept {
  return "vector_store";
}

bool VectorStore::ready() const noexcept {
  return options_.enabled && !options_.host.empty() && options_.port != 0 &&
         !options_.collection.empty() && options_.vector_size != 0 &&
         options_.timeout.count() > 0;
}

const VectorStore::Options& VectorStore::options() const noexcept {
  return options_;
}

void VectorStore::ensure_collection() const {
  if (!ready()) {
    return;
  }

  std::call_once(collection_once_, [this] { ensure_collection_impl(); });
}

void VectorStore::upsert_record(const Record& record) const {
  upsert_records(std::span<const Record>{&record, 1});
}

void VectorStore::upsert_records(std::span<const Record> records) const {
  if (!ready() || records.empty()) {
    return;
  }

  ensure_collection();

  nlohmann::json points = nlohmann::json::array();
  for (const auto& record : records) {
    if (record.embedding.size() != options_.vector_size) {
      std::ostringstream oss;
      oss << "VectorStore expected embeddings with dimension " << options_.vector_size
          << " but got " << record.embedding.size() << " for " << record.symbol.qualified_name;
      throw std::runtime_error(oss.str());
    }

    nlohmann::json payload;
    payload["symbol_id"] = record.symbol_id;
    payload["file_path"] = record.symbol.file_path;
    payload["module_name"] = record.symbol.module_name;
    payload["module_path"] = record.symbol.module_path;
    payload["kind"] = static_cast<std::uint8_t>(record.symbol.kind);
    payload["qualified_name"] = record.symbol.qualified_name;
    payload["signature"] = record.symbol.signature;
    payload["start_line"] = record.symbol.start_line;
    payload["end_line"] = record.symbol.end_line;
    payload["source_text"] = record.source_text;

    nlohmann::json point;
    point["id"] = record.symbol_id;
    point["vector"] = record.embedding;
    point["payload"] = std::move(payload);
    points.push_back(std::move(point));
  }

  const nlohmann::json body{{"points", points}};
  (void)request_json(http::verb::put,
                     make_path("/collections/" + options_.collection + "/points?wait=true"),
                     &body);
}

void VectorStore::delete_file(std::string_view file_path) const {
  if (!ready() || file_path.empty()) {
    return;
  }

  ensure_collection();

  const auto filter = nlohmann::json::parse(build_file_filter(file_path));
  const nlohmann::json body{{"filter", filter}};
  (void)request_json(http::verb::post,
                     make_path("/collections/" + options_.collection + "/points/delete?wait=true"),
                     &body);
}

void VectorStore::delete_files(std::span<const std::filesystem::path> file_paths) const {
  if (!ready() || file_paths.empty()) {
    return;
  }

  for (const auto& file_path : file_paths) {
    delete_file(file_path.generic_string());
  }
}

std::vector<VectorStore::SearchHit>
VectorStore::search(const Embedder::Embedding& query_embedding, std::size_t limit) const {
  if (!ready() || query_embedding.empty() || limit == 0) {
    return {};
  }

  if (query_embedding.size() != options_.vector_size) {
    std::ostringstream oss;
    oss << "VectorStore expected query embeddings with dimension " << options_.vector_size
        << " but got " << query_embedding.size();
    throw std::runtime_error(oss.str());
  }

  ensure_collection();

  nlohmann::json body;
  body["vector"] = nlohmann::json::array();
  for (const auto value : query_embedding) {
    body["vector"].push_back(value);
  }
  body["limit"] = limit;
  body["with_payload"] = true;
  body["with_vector"] = false;

  const auto response =
      request_json(http::verb::post,
                   make_path("/collections/" + options_.collection + "/points/search"), &body);

  std::vector<SearchHit> hits;
  if (!response.contains("result") || !response["result"].is_array()) {
    return hits;
  }

  hits.reserve(response["result"].size());
  for (const auto& row : response["result"]) {
    hits.push_back(parse_search_hit(row));
  }
  return hits;
}

std::string VectorStore::make_path(std::string_view suffix) {
  return std::string{suffix};
}

std::string VectorStore::join_host_port(std::string_view host, std::uint16_t port) {
  std::ostringstream oss;
  oss << host << ':' << port;
  return oss.str();
}

nlohmann::json VectorStore::request_json(http::verb method, std::string_view path,
                                         const nlohmann::json* body) const {
  asio::io_context io;
  tcp::resolver resolver{io};
  beast::tcp_stream connection{io};
  boost::system::error_code ec;

  const auto results = resolver.resolve(options_.host, std::to_string(options_.port), ec);
  if (ec) {
    throw std::runtime_error("Failed to resolve Qdrant endpoint: " + ec.message());
  }

  connection.expires_after(options_.timeout);
  connection.connect(results, ec);
  if (ec) {
    throw std::runtime_error("Failed to connect to Qdrant endpoint: " + ec.message());
  }

  http::request<http::string_body> request{method, std::string(path), 11};
  request.set(http::field::host, join_host_port(options_.host, options_.port));
  request.set(http::field::user_agent, "QodeLoc/0.1");
  request.set(http::field::accept, "application/json");
  request.set(http::field::connection, "close");
  if (!options_.api_key.empty()) {
    request.set("api-key", options_.api_key);
  }
  if (body != nullptr) {
    request.set(http::field::content_type, "application/json");
    request.body() = body->dump();
  }
  request.prepare_payload();

  connection.expires_after(options_.timeout);
  http::write(connection, request, ec);
  if (ec) {
    close_stream(connection);
    throw std::runtime_error("Failed to send Qdrant request: " + ec.message());
  }

  beast::flat_buffer buffer;
  http::response_parser<http::string_body> parser;
  parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

  connection.expires_after(options_.timeout);
  http::read(connection, buffer, parser, ec);
  if (ec) {
    close_stream(connection);
    throw std::runtime_error("Failed while reading Qdrant response: " + ec.message());
  }

  const auto& response = parser.get();
  if (response.result_int() < 200 || response.result_int() >= 300) {
    std::ostringstream oss;
    oss << "Qdrant request " << path << " failed with HTTP " << response.result_int();
    if (!response.body().empty()) {
      oss << ": " << response.body();
    }
    close_stream(connection);
    throw std::runtime_error(oss.str());
  }

  nlohmann::json json = nlohmann::json::parse(response.body().empty() ? "{}" : response.body());
  close_stream(connection);
  return json;
}

VectorStore::SearchHit VectorStore::parse_search_hit(const nlohmann::json& json) const {
  SearchHit hit;
  const auto id = json.value("id", std::int64_t{0});
  hit.record.symbol_id = id;
  hit.score = json.value("score", 0.0);

  if (json.contains("payload") && json["payload"].is_object()) {
    hit.record = parse_record(json["payload"], hit.record.symbol_id);
  }

  return hit;
}

VectorStore::Record VectorStore::parse_record(const nlohmann::json& payload,
                                              SymbolId fallback_id) {
  Record record;
  record.symbol_id = payload.value("symbol_id", fallback_id);
  record.symbol.file_path = payload.value("file_path", std::string{});
  record.symbol.module_name = payload.value("module_name", std::string{});
  record.symbol.module_path = payload.value("module_path", std::string{});
  record.symbol.kind = static_cast<SymbolKind>(payload.value("kind", 0));
  record.symbol.qualified_name = payload.value("qualified_name", std::string{});
  record.symbol.signature = payload.value("signature", std::string{});
  record.symbol.start_line = payload.value("start_line", std::uint32_t{});
  record.symbol.end_line = payload.value("end_line", std::uint32_t{});
  record.source_text = payload.value("source_text", std::string{});
  return record;
}

void VectorStore::ensure_collection_impl() const {
  const auto collection_path = make_path("/collections/" + options_.collection);
  try {
    (void)request_json(http::verb::get, collection_path, nullptr);
    return;
  } catch (const std::exception& error) {
    const std::string_view message{error.what()};
    if (message.find("HTTP 404") == std::string_view::npos) {
      throw;
    }
  }

  nlohmann::json body;
  body["vectors"] = {{"size", options_.vector_size}, {"distance", "Cosine"}};
  body["on_disk_payload"] = false;
  (void)request_json(http::verb::put, collection_path, &body);
}

} // namespace qodeloc::core
