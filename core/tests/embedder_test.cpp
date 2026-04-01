#include <atomic>
#include <cmath>
#include <cstddef>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <httplib/httplib.h>
#include <initializer_list>
#include <mutex>
#include <nlohmann/json.hpp>
#include <qodeloc/core/embedder.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace qodeloc::core {
namespace {

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
  EmbeddingServer() {
    server_.set_logger([](const auto&, const auto&) {});
    server_.Post("/v1/embeddings", [this](const httplib::Request& request,
                                          httplib::Response& response) {
      const auto payload = nlohmann::json::parse(request.body);
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

      response.set_content(reply.dump(), "application/json");
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    if (port_ <= 0) {
      throw std::runtime_error("Failed to bind embedding test server");
    }

    server_thread_ = std::thread([this] { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  EmbeddingServer(const EmbeddingServer&) = delete;
  EmbeddingServer& operator=(const EmbeddingServer&) = delete;

  ~EmbeddingServer() {
    server_.stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  [[nodiscard]] std::uint16_t port() const noexcept {
    return static_cast<std::uint16_t>(port_);
  }

  [[nodiscard]] std::size_t request_count() const noexcept {
    return request_count_.load();
  }

  [[nodiscard]] std::vector<std::size_t> batch_sizes() const {
    std::lock_guard lock(mutex_);
    return batch_sizes_;
  }

private:
  httplib::Server server_;
  std::thread server_thread_;
  int port_{};
  mutable std::mutex mutex_;
  std::vector<std::size_t> batch_sizes_;
  std::atomic<std::size_t> request_count_{0};
};

} // namespace

TEST(EmbedderTest, ReadyReportsConfiguredEndpoint) {
  const Embedder embedder;

  EXPECT_TRUE(embedder.ready());
  EXPECT_EQ(embedder.module_name(), "embedder");
}

TEST(EmbedderTest, BatchesRequestsAndPreservesOrder) {
  EmbeddingServer server;
  Embedder::Options options;
  options.host = "127.0.0.1";
  options.port = server.port();
  options.api_path = "/v1/embeddings";
  options.model = "qodeloc-embedding";
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
  Embedder::Options options;
  options.host = "127.0.0.1";
  options.port = server.port();
  options.api_path = "/v1/embeddings";
  options.model = "qodeloc-embedding";
  options.batch_size = 8;
  options.timeout = std::chrono::seconds{5};

  Embedder embedder{options};

  const auto embedding = embedder.embed("void dispatch_event(Event e) { handle_event(e); }");

  EXPECT_EQ(server.request_count(), 1U);
  EXPECT_THAT(server.batch_sizes(), ElementsAre(1U));
  EXPECT_EQ(embedding, (Embedder::Embedding{8.0F, 1.0F, 0.0F}));
}

} // namespace qodeloc::core
