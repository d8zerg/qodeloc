#include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/embedder.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

[[nodiscard]] std::string join_host_port(std::string_view host, std::uint16_t port) {
  std::ostringstream oss;
  oss << host << ':' << port;
  return oss.str();
}

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

} // namespace

Embedder::Embedder() : Embedder(Config::current().embedder_options()) {}

Embedder::Embedder(Options options) : options_(std::move(options)) {
  if (options_.host.empty()) {
    throw std::invalid_argument("Embedder host must not be empty");
  }
  if (options_.port == 0) {
    throw std::invalid_argument("Embedder port must be greater than zero");
  }
  if (options_.api_path.empty()) {
    throw std::invalid_argument("Embedder API path must not be empty");
  }
  if (options_.model.empty()) {
    throw std::invalid_argument("Embedder model must not be empty");
  }
  if (options_.batch_size == 0) {
    throw std::invalid_argument("Embedder batch size must be greater than zero");
  }
  if (options_.timeout.count() <= 0) {
    throw std::invalid_argument("Embedder timeout must be positive");
  }
}

std::string_view Embedder::module_name() const noexcept {
  return "embedder";
}

bool Embedder::ready() const noexcept {
  return !options_.host.empty() && options_.port != 0 && !options_.api_path.empty() &&
         !options_.model.empty() && options_.batch_size != 0 && options_.timeout.count() > 0;
}

const Embedder::Options& Embedder::options() const noexcept {
  return options_;
}

Embedder::Embedding Embedder::embed(std::string_view text) const {
  std::vector<std::string> single_text{std::string{text}};
  auto batch = embed_batch(single_text);
  if (batch.empty()) {
    return {};
  }

  return std::move(batch.front());
}

Embedder::Embeddings Embedder::embed_batch(std::span<const std::string> texts) const {
  Embeddings result;
  result.reserve(texts.size());

  for (std::size_t offset = 0; offset < texts.size(); offset += options_.batch_size) {
    const auto count = std::min(options_.batch_size, texts.size() - offset);
    std::vector<std::string> chunk;
    chunk.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      chunk.emplace_back(texts[offset + index]);
    }

    auto batch = request_batch(chunk);
    if (batch.size() != count) {
      std::ostringstream oss;
      oss << "Embedding backend returned " << batch.size() << " vectors for " << count << " inputs";
      throw std::runtime_error(oss.str());
    }

    result.insert(result.end(), std::make_move_iterator(batch.begin()),
                  std::make_move_iterator(batch.end()));
  }

  return result;
}

Embedder::Embeddings Embedder::request_batch(std::span<const std::string> texts) const {
  nlohmann::json request;
  request["model"] = options_.model;
  request["encoding_format"] = "float";
  if (texts.size() == 1) {
    request["input"] = texts.front();
  } else {
    request["input"] = nlohmann::json::array();
    for (const auto& text : texts) {
      request["input"].push_back(text);
    }
  }

  asio::io_context io;
  tcp::resolver resolver{io};
  beast::tcp_stream connection{io};
  boost::system::error_code ec;

  const auto results = resolver.resolve(options_.host, std::to_string(options_.port), ec);
  if (ec) {
    throw std::runtime_error("Failed to resolve embedding endpoint: " + ec.message());
  }

  connection.expires_after(options_.timeout);
  connection.connect(results, ec);
  if (ec) {
    throw std::runtime_error("Failed to connect to embedding endpoint: " + ec.message());
  }

  http::request<http::string_body> http_request{http::verb::post, options_.api_path, 11};
  http_request.set(http::field::host, join_host_port(options_.host, options_.port));
  http_request.set(http::field::user_agent, "QodeLoc/0.1");
  http_request.set(http::field::content_type, "application/json");
  http_request.set(http::field::accept, "application/json");
  http_request.set(http::field::connection, "close");
  http_request.body() = request.dump();
  http_request.prepare_payload();

  connection.expires_after(options_.timeout);
  http::write(connection, http_request, ec);
  if (ec) {
    close_stream(connection);
    throw std::runtime_error("Failed to send embedding request: " + ec.message());
  }

  beast::flat_buffer buffer;
  http::response_parser<http::string_body> parser;
  parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

  connection.expires_after(options_.timeout);
  http::read(connection, buffer, parser, ec);
  if (ec) {
    close_stream(connection);
    throw std::runtime_error("Failed while reading embedding response: " + ec.message());
  }

  const auto& response = parser.get();
  if (response.result_int() < 200 || response.result_int() >= 300) {
    std::ostringstream oss;
    oss << "Embedding backend at http://" << options_.host << ':' << options_.port
        << options_.api_path << " returned HTTP " << response.result_int();
    if (!response.body().empty()) {
      oss << ": " << response.body();
    }
    close_stream(connection);
    throw std::runtime_error(oss.str());
  }

  auto embeddings = parse_embeddings_response(response.body(), texts.size());
  close_stream(connection);
  return embeddings;
}

Embedder::Embeddings Embedder::parse_embeddings_response(const std::string& body,
                                                         std::size_t expected_count) {
  if (expected_count == 0) {
    return {};
  }

  const auto json = nlohmann::json::parse(body);

  if (json.contains("data") && json["data"].is_array()) {
    struct Item {
      std::size_t index{};
      Embedding embedding;
    };

    std::vector<Item> items;
    items.reserve(json["data"].size());

    for (const auto& row : json["data"]) {
      Item item;
      item.index = row.value("index", items.size());
      item.embedding = row.at("embedding").get<Embedding>();
      items.push_back(std::move(item));
    }

    if (items.size() != expected_count) {
      std::ostringstream oss;
      oss << "Embedding backend returned " << items.size() << " rows for " << expected_count
          << " inputs";
      throw std::runtime_error(oss.str());
    }

    std::sort(items.begin(), items.end(),
              [](const Item& lhs, const Item& rhs) { return lhs.index < rhs.index; });

    Embeddings embeddings;
    embeddings.reserve(items.size());
    for (auto& item : items) {
      embeddings.push_back(std::move(item.embedding));
    }
    return embeddings;
  }

  if (json.contains("embedding") && expected_count == 1) {
    return {json.at("embedding").get<Embedding>()};
  }

  throw std::runtime_error("Embedding backend returned an unexpected JSON shape");
}

} // namespace qodeloc::core
