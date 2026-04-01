#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/llm.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class MockLlmServer {
public:
  using Handler =
      std::function<void(const http::request<http::string_body>&, tcp::socket&, std::size_t)>;

  explicit MockLlmServer(Handler handler)
      : acceptor_(io_, tcp::endpoint(tcp::v4(), 0)), handler_(std::move(handler)) {
    port_ = static_cast<std::uint16_t>(acceptor_.local_endpoint().port());
    thread_ = std::thread([this] { serve(); });
  }

  MockLlmServer(const MockLlmServer&) = delete;
  MockLlmServer& operator=(const MockLlmServer&) = delete;

  ~MockLlmServer() {
    stop();
  }

  [[nodiscard]] std::uint16_t port() const noexcept {
    return port_;
  }

private:
  void serve() {
    do_accept();
    io_.run();
  }

  void do_accept() {
    auto socket = std::make_shared<tcp::socket>(io_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
      if (stopped_.load(std::memory_order_relaxed)) {
        return;
      }
      if (!ec) {
        handle(std::move(*socket));
      }
      if (!stopped_.load(std::memory_order_relaxed)) {
        do_accept();
      }
    });
  }

  void handle(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    boost::system::error_code ec;
    http::read(socket, buffer, request, ec);
    if (ec) {
      return;
    }

    handler_(request, socket, request_count_.fetch_add(1, std::memory_order_relaxed));
  }

  void stop() {
    bool expected = false;
    if (stopped_.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
      boost::system::error_code ec;
      acceptor_.close(ec);
      io_.stop();
      if (thread_.joinable()) {
        thread_.join();
      }
    } else if (thread_.joinable()) {
      io_.stop();
      thread_.join();
    }
  }

  asio::io_context io_;
  tcp::acceptor acceptor_;
  Handler handler_;
  std::thread thread_;
  std::atomic<bool> stopped_{false};
  std::atomic<std::size_t> request_count_{0};
  std::uint16_t port_{};
};

void write_json_response(tcp::socket& socket, int status, const nlohmann::json& body) {
  http::response<http::string_body> response{static_cast<http::status>(status), 11};
  response.set(http::field::server, "qodeloc-test");
  response.set(http::field::content_type, "application/json");
  response.keep_alive(false);
  response.body() = body.dump();
  response.prepare_payload();
  boost::system::error_code ec;
  http::write(socket, response, ec);
}

void write_text_response(tcp::socket& socket, int status, std::string body) {
  http::response<http::string_body> response{static_cast<http::status>(status), 11};
  response.set(http::field::server, "qodeloc-test");
  response.set(http::field::content_type, "text/plain");
  response.keep_alive(false);
  response.body() = std::move(body);
  response.prepare_payload();
  boost::system::error_code ec;
  http::write(socket, response, ec);
}

void write_chunked_sse_response(tcp::socket& socket, std::initializer_list<std::string> events) {
  const std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Transfer-Encoding: chunked\r\n"
                             "Connection: close\r\n\r\n";
  asio::write(socket, asio::buffer(header));

  for (const auto& event : events) {
    std::ostringstream chunk;
    chunk << std::hex << event.size() << "\r\n" << event << "\r\n";
    const auto chunk_text = chunk.str();
    asio::write(socket, asio::buffer(chunk_text));
  }

  asio::write(socket, asio::buffer(std::string("0\r\n\r\n")));
}

[[nodiscard]] LlmClient::ChatRequest make_request() {
  LlmClient::ChatRequest request;
  request.messages.push_back({"system", "You are QodeLoc."});
  request.messages.push_back({"user", "Explain the renderer."});
  return request;
}

} // namespace

TEST(LlmClientTest, ReadyReportsConfiguredEndpoint) {
  const LlmClient client;

  EXPECT_TRUE(client.ready());
  EXPECT_EQ(client.module_name(), "llm");
}

TEST(LlmClientTest, RetriesTransientErrorsAndParsesCompletion) {
  std::atomic<std::size_t> request_count{0};
  MockLlmServer server([&](const http::request<http::string_body>& request, tcp::socket& socket,
                           std::size_t request_index) {
    EXPECT_EQ(request.method(), http::verb::post);
    EXPECT_EQ(request.target(), "/v1/chat/completions");
    EXPECT_EQ(request[http::field::authorization], "Bearer sk-qodeloc-dev");

    const auto payload = nlohmann::json::parse(request.body());
    EXPECT_EQ(payload.value("model", ""), "qodeloc-local");
    EXPECT_FALSE(payload.value("stream", true));
    ASSERT_TRUE(payload.contains("messages"));
    EXPECT_EQ(payload["messages"].size(), 2U);

    request_count.fetch_add(1, std::memory_order_relaxed);
    if (request_index == 0) {
      write_text_response(socket, 503, "transient");
      return;
    }

    nlohmann::json response;
    response["id"] = "chatcmpl-test";
    response["choices"] = nlohmann::json::array();
    response["choices"].push_back(
        {{"index", 0},
         {"message", {{"role", "assistant"}, {"content", "Renderer summary"}}},
         {"finish_reason", "stop"}});
    write_json_response(socket, 200, response);
  });

  auto options = Config::current().llm_options();
  options.port = server.port();
  options.timeout = std::chrono::seconds{5};
  options.max_retries = 1;
  options.initial_backoff = std::chrono::milliseconds{1};
  options.max_backoff = std::chrono::milliseconds{2};

  LlmClient client{options};
  const auto response = client.complete(make_request());

  EXPECT_EQ(request_count.load(), 2U);
  EXPECT_EQ(response.content, "Renderer summary");
  ASSERT_TRUE(response.raw.contains("choices"));
  EXPECT_EQ(response.raw["choices"].front()["message"]["content"], "Renderer summary");
}

TEST(LlmClientTest, StreamsSseChunksAndInvokesCallback) {
  MockLlmServer server([&](const http::request<http::string_body>& request, tcp::socket& socket,
                           std::size_t) {
    const auto payload = nlohmann::json::parse(request.body());
    EXPECT_TRUE(payload.value("stream", false));

    write_chunked_sse_response(socket,
                               {"data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\n",
                                "data: {\"choices\":[{\"delta\":{\"content\":\"world\"}}]}\n\n",
                                "data: [DONE]\n\n"});
  });

  auto options = Config::current().llm_options();
  options.port = server.port();
  options.timeout = std::chrono::seconds{5};

  LlmClient client{options};
  std::vector<std::string> chunks;
  const auto response = client.stream(make_request(), [&](std::string_view chunk) {
    chunks.emplace_back(chunk);
    return true;
  });

  EXPECT_THAT(chunks, ::testing::ElementsAre("Hello ", "world"));
  EXPECT_EQ(response.content, "Hello world");
  EXPECT_THAT(response.raw.dump(), ::testing::HasSubstr("world"));
}

} // namespace qodeloc::core
