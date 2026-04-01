#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <cmath>
#include <cstddef>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/embedder.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

using ::testing::ElementsAre;

[[nodiscard]] bool contains_any(std::string_view text,
                                std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (text.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] Embedder::Embedding synthetic_embedding(std::string_view text) {
  if (contains_any(text, {"dispatch_event", "handle_event", "publish_event", "event"})) {
    return {8.0F, 1.0F, 0.0F};
  }

  if (contains_any(text, {"allocate_buffer", "release_buffer", "memory", "buffer"})) {
    return {0.0F, 8.0F, 1.0F};
  }

  return {0.0F, 1.0F, 8.0F};
}

[[nodiscard]] double cosine_similarity(const Embedder::Embedding& lhs,
                                       const Embedder::Embedding& rhs) {
  if (lhs.size() != rhs.size()) {
    throw std::runtime_error("Embedding dimensions must match");
  }

  double dot = 0.0;
  double lhs_norm = 0.0;
  double rhs_norm = 0.0;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    dot += static_cast<double>(lhs[i]) * static_cast<double>(rhs[i]);
    lhs_norm += static_cast<double>(lhs[i]) * static_cast<double>(lhs[i]);
    rhs_norm += static_cast<double>(rhs[i]) * static_cast<double>(rhs[i]);
  }

  return dot / (std::sqrt(lhs_norm) * std::sqrt(rhs_norm));
}

class EmbeddingServer {
public:
  EmbeddingServer() : acceptor_(io_, tcp::endpoint(tcp::v4(), 0)) {
    port_ = static_cast<std::uint16_t>(acceptor_.local_endpoint().port());
    thread_ = std::thread([this] { serve(); });
  }

  EmbeddingServer(const EmbeddingServer&) = delete;
  EmbeddingServer& operator=(const EmbeddingServer&) = delete;

  ~EmbeddingServer() {
    stop();
  }

  [[nodiscard]] std::uint16_t port() const noexcept {
    return port_;
  }

  [[nodiscard]] std::size_t request_count() const noexcept {
    return request_count_.load();
  }

  [[nodiscard]] std::vector<std::size_t> batch_sizes() const {
    std::lock_guard lock(mutex_);
    return batch_sizes_;
  }

private:
  void serve() {
    while (!stopped_.load(std::memory_order_relaxed)) {
      tcp::socket socket{io_};
      boost::system::error_code ec;
      acceptor_.accept(socket, ec);
      if (stopped_.load(std::memory_order_relaxed)) {
        socket.close(ec);
        break;
      }
      if (ec) {
        continue;
      }

      handle(std::move(socket));
    }
  }

  void handle(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    boost::system::error_code ec;
    http::read(socket, buffer, request, ec);
    if (ec) {
      return;
    }

    const auto payload = nlohmann::json::parse(request.body());
    std::vector<std::string> inputs;
    const auto& input = payload.at("input");
    if (input.is_string()) {
      inputs.push_back(input.get<std::string>());
    } else {
      for (const auto& item : input) {
        inputs.push_back(item.get<std::string>());
      }
    }

    {
      std::lock_guard lock(mutex_);
      batch_sizes_.push_back(inputs.size());
      request_count_.fetch_add(1, std::memory_order_relaxed);
    }

    nlohmann::json reply;
    reply["object"] = "list";
    reply["model"] = payload.value("model", "qodeloc-embedding");
    reply["data"] = nlohmann::json::array();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      reply["data"].push_back(
          {{"object", "embedding"}, {"index", i}, {"embedding", synthetic_embedding(inputs[i])}});
    }

    http::response<http::string_body> response{http::status::ok, 11};
    response.set(http::field::server, "qodeloc-test");
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = reply.dump();
    response.prepare_payload();
    http::write(socket, response, ec);
  }

  void stop() {
    bool expected = false;
    if (stopped_.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
      boost::system::error_code ec;
      if (port_ != 0) {
        asio::io_context wake_io;
        tcp::resolver resolver{wake_io};
        tcp::socket wake_socket{wake_io};
        const auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port_), ec);
        if (!ec) {
          asio::connect(wake_socket, endpoints, ec);
        }
      }
      if (thread_.joinable()) {
        thread_.join();
      }
      acceptor_.close(ec);
      io_.stop();
    } else if (thread_.joinable()) {
      io_.stop();
      thread_.join();
    }
  }

  asio::io_context io_;
  tcp::acceptor acceptor_;
  std::thread thread_;
  std::atomic<bool> stopped_{false};
  std::atomic<std::size_t> request_count_{0};
  std::uint16_t port_{};
  mutable std::mutex mutex_;
  std::vector<std::size_t> batch_sizes_;
};

} // namespace

TEST(EmbedderTest, ReadyReportsConfiguredEndpoint) {
  const auto options = Config::current().embedder_options();
  const Embedder embedder{options};

  EXPECT_TRUE(embedder.ready());
  EXPECT_EQ(embedder.module_name(), "embedder");
}

TEST(EmbedderTest, BatchesRequestsAndPreservesOrder) {
  EmbeddingServer server;
  auto options = Config::current().embedder_options();
  options.port = server.port();
  options.batch_size = 2;
  options.timeout = std::chrono::seconds{5};

  Embedder embedder{options};

  const std::vector<std::string> texts{
      "void dispatch_event(Event e) { handle_event(e); }",
      "void handle_event(Event e) { publish_event(e); }",
      "std::unique_ptr<char[]> allocate_buffer(std::size_t size) { return {}; }",
  };

  const auto embeddings = embedder.embed_batch(texts);

  EXPECT_THAT(server.batch_sizes(), ElementsAre(2U, 1U));
  EXPECT_EQ(server.request_count(), 2U);
  ASSERT_EQ(embeddings.size(), 3U);
  EXPECT_GT(cosine_similarity(embeddings[0], embeddings[1]), 0.99);
  EXPECT_LT(cosine_similarity(embeddings[0], embeddings[2]), 0.3);
}

TEST(EmbedderTest, SingleEmbedUsesTheSameTransport) {
  EmbeddingServer server;
  auto options = Config::current().embedder_options();
  options.port = server.port();
  options.batch_size = 8;
  options.timeout = std::chrono::seconds{5};

  Embedder embedder{options};

  const auto embedding = embedder.embed("void dispatch_event(Event e) { handle_event(e); }");

  EXPECT_EQ(server.request_count(), 1U);
  EXPECT_THAT(server.batch_sizes(), ElementsAre(1U));
  EXPECT_EQ(embedding, (Embedder::Embedding{8.0F, 1.0F, 0.0F}));
}

} // namespace qodeloc::core
