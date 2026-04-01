#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <nlohmann/json.hpp>
#include <qodeloc/core/api.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/prompt_builder.hpp>
#include <qodeloc/core/retriever.hpp>
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
using json = nlohmann::json;

using ::testing::HasSubstr;

class TempWorkspace {
public:
  TempWorkspace()
      : root_(std::filesystem::temp_directory_path() /
              ("qodeloc-api-tests-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
    std::filesystem::create_directories(root_);
  }

  TempWorkspace(const TempWorkspace&) = delete;
  TempWorkspace& operator=(const TempWorkspace&) = delete;

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

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
      asio::io_context wakeup_io;
      tcp::socket wakeup_socket{wakeup_io};
      tcp::resolver resolver{wakeup_io};
      const auto results = resolver.resolve("127.0.0.1", std::to_string(port_));
      wakeup_socket.connect(results.begin()->endpoint(), ec);
      io_.stop();
      if (thread_.joinable()) {
        thread_.join();
      }
    } else if (thread_.joinable()) {
      boost::system::error_code ec;
      acceptor_.close(ec);
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

[[nodiscard]] Embedder::Embedding make_embedding(std::initializer_list<float> values) {
  return Embedder::Embedding(values.begin(), values.end());
}

void write_source_file(const std::filesystem::path& file_path, std::string_view content) {
  std::filesystem::create_directories(file_path.parent_path());
  std::ofstream output(file_path);
  ASSERT_TRUE(output.is_open());
  output << content;
}

[[nodiscard]] json post_json(std::uint16_t port, std::string target, const json& body) {
  asio::io_context io;
  tcp::resolver resolver{io};
  beast::tcp_stream stream{io};
  stream.expires_after(std::chrono::seconds{5});

  const auto results = resolver.resolve("127.0.0.1", std::to_string(port));
  stream.connect(results);

  http::request<http::string_body> request{http::verb::post, std::move(target), 11};
  request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
  request.set(http::field::user_agent, "qodeloc-api-test");
  request.set(http::field::content_type, "application/json");
  request.set(http::field::accept, "application/json");
  request.body() = body.dump();
  request.prepare_payload();

  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  stream.expires_after(std::chrono::seconds{5});
  http::read(stream, buffer, response);

  boost::system::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);

  EXPECT_EQ(response.result_int(), 200);
  return json::parse(response.body());
}

[[nodiscard]] json get_json(std::uint16_t port, std::string target) {
  asio::io_context io;
  tcp::resolver resolver{io};
  beast::tcp_stream stream{io};
  stream.expires_after(std::chrono::seconds{5});

  const auto results = resolver.resolve("127.0.0.1", std::to_string(port));
  stream.connect(results);

  http::request<http::string_body> request{http::verb::get, std::move(target), 11};
  request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
  request.set(http::field::user_agent, "qodeloc-api-test");
  request.set(http::field::accept, "application/json");

  http::write(stream, request);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  stream.expires_after(std::chrono::seconds{5});
  http::read(stream, buffer, response);

  boost::system::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);

  EXPECT_EQ(response.result_int(), 200);
  return json::parse(response.body());
}

[[nodiscard]] Embedder::Embeddings
module_embedding_batch_fn(std::span<const std::string> summaries) {
  Embedder::Embeddings embeddings;
  embeddings.reserve(summaries.size());
  for (const auto& summary : summaries) {
    if (summary.contains("namespace app") || summary.contains("Widget")) {
      embeddings.push_back(make_embedding({0.0F, 1.0F, 0.0F}));
    } else if (summary.contains("namespace math") || summary.contains("format_value")) {
      embeddings.push_back(make_embedding({1.0F, 0.0F, 0.0F}));
    } else {
      embeddings.push_back(make_embedding({0.0F, 0.0F, 1.0F}));
    }
  }
  return embeddings;
}

[[nodiscard]] Retriever::QueryEmbeddingFn query_embedding_fn() {
  return [](std::string_view query) {
    if (query.contains("format") || query.contains("value") || query.contains("widget") ||
        query.contains("math")) {
      return make_embedding({1.0F, 0.0F, 0.0F});
    }
    return make_embedding({0.0F, 1.0F, 0.0F});
  };
}

void append_touch_function(const std::filesystem::path& file_path, std::string_view function_name,
                           int return_value) {
  std::ofstream output(file_path, std::ios::app);
  ASSERT_TRUE(output.is_open());
  output << "\nstatic int " << function_name << "() { return " << return_value << "; }\n";
}

} // namespace

TEST(ApiServerTest, ServesSearchExplainDepsModuleStatusAndReindex) {
  TempWorkspace workspace;
  const auto repo_root = workspace.root() / "repo";
  const auto source_root = std::filesystem::absolute(std::filesystem::path{__FILE__})
                               .parent_path()
                               .parent_path()
                               .parent_path();
  const auto config = Config::load(source_root / ".env");

  write_source_file(repo_root / "math" / "math.hpp",
                    R"(#pragma once
#include <string>

namespace math {

std::string format_value(int value);

class Base {
public:
  virtual std::string name() const = 0;
};

} // namespace math
)");

  write_source_file(repo_root / "math" / "math.cpp",
                    R"(#include "math.hpp"

namespace math {

std::string format_value(int value) {
  return std::to_string(value);
}

} // namespace math
)");

  write_source_file(repo_root / "app" / "widget.cpp",
                    R"(#include "../math/math.hpp"

namespace app {

class Widget : public math::Base {
public:
  std::string name() const override {
    return format_value(7);
  }
};

} // namespace app
)");

  auto indexer_options = config.indexer_options(repo_root);
  indexer_options.embedding_batch_size = 2;
  indexer_options.recursive = true;

  std::vector<std::size_t> batch_sizes;
  Indexer indexer{indexer_options, {}, [&batch_sizes](std::span<const std::string> texts) {
                    batch_sizes.push_back(texts.size());
                    Embedder::Embeddings embeddings;
                    embeddings.reserve(texts.size());
                    for (const auto& text : texts) {
                      if (text.contains("format_value")) {
                        embeddings.push_back({1.0F, 0.0F, 0.0F});
                      } else if (text.contains("Widget") || text.contains("name()")) {
                        embeddings.push_back({0.0F, 1.0F, 0.0F});
                      } else {
                        embeddings.push_back({0.0F, 0.0F, 1.0F});
                      }
                    }
                    return embeddings;
                  }};

  ASSERT_TRUE(indexer.ready());
  const auto initial = indexer.index();
  EXPECT_EQ(initial.stats.parse_errors, 0U);
  EXPECT_GT(initial.stats.symbols_indexed, 0U);

  Retriever retriever{config.retriever_options(), query_embedding_fn()};
  retriever.attach_storage(indexer.storage());
  retriever.build(indexer.symbols(), module_embedding_batch_fn);
  ASSERT_TRUE(retriever.ready());

  MockLlmServer llm_server([](const http::request<http::string_body>& request, tcp::socket& socket,
                              std::size_t request_index) {
    EXPECT_EQ(request.method(), http::verb::post);
    EXPECT_EQ(request.target(), "/v1/chat/completions");
    EXPECT_EQ(request[http::field::authorization], "Bearer sk-qodeloc-dev");
    const auto payload = json::parse(request.body());
    EXPECT_FALSE(payload.value("stream", true));
    EXPECT_TRUE(payload.contains("messages"));
    EXPECT_GT(payload["messages"].size(), 0U);
    EXPECT_EQ(request_index, 0U);

    json response;
    response["choices"] = json::array();
    response["choices"].push_back(
        {{"index", 0},
         {"message", {{"role", "assistant"}, {"content", "Widget explanation"}}},
         {"finish_reason", "stop"}});
    http::response<http::string_body> http_response{http::status::ok, 11};
    http_response.set(http::field::server, "qodeloc-test");
    http_response.set(http::field::content_type, "application/json");
    http_response.keep_alive(false);
    http_response.body() = response.dump();
    http_response.prepare_payload();
    boost::system::error_code ec;
    http::write(socket, http_response, ec);
  });

  auto llm_options = config.llm_options();
  llm_options.port = llm_server.port();
  llm_options.timeout = std::chrono::seconds{5};
  llm_options.max_retries = 0;
  LlmClient llm_client{llm_options};

  auto api_options = config.api_options();
  api_options.port = 0;
  api_options.request_timeout = std::chrono::seconds{5};

  PromptBuilder prompt_builder{config.prompt_builder_options()};
  ApiServer api_server{api_options};
  api_server.attach_indexer(indexer);
  api_server.attach_retriever(retriever);
  api_server.attach_prompt_builder(prompt_builder);
  api_server.attach_llm_client(llm_client);
  api_server.attach_module_embedding_batch(module_embedding_batch_fn);

  ASSERT_TRUE(api_server.ready());
  api_server.start();
  const auto port = api_server.bound_port();
  ASSERT_NE(port, 0U);

  const auto status = get_json(port, "/status");
  EXPECT_TRUE(status.value("running", false));
  EXPECT_EQ(status.value("symbol_count", 0U), indexer.storage().graph().symbol_count());
  EXPECT_TRUE(status.value("retriever_ready", false));
  EXPECT_TRUE(status.value("llm_ready", false));

  const auto search = post_json(port, "/search", {{"query", "format value"}});
  EXPECT_EQ(search.value("query", ""), "format value");
  ASSERT_FALSE(search["modules"].empty());
  ASSERT_FALSE(search["symbols"].empty());
  EXPECT_THAT(search.dump(), HasSubstr("math::format_value"));

  const auto callers = post_json(port, "/callers", {{"name", "math::format_value"}});
  ASSERT_FALSE(callers["callers"].empty());
  EXPECT_THAT(callers.dump(), HasSubstr("app::Widget::name"));

  const auto deps = post_json(port, "/deps", {{"name", "app::Widget::name"}, {"depth", 2}});
  EXPECT_THAT(deps.dump(), HasSubstr("math"));
  EXPECT_THAT(deps.dump(), HasSubstr("module_dependencies"));

  const auto module = post_json(port, "/module", {{"module_name", "app"}});
  EXPECT_EQ(module.value("module_name", ""), "app");
  ASSERT_FALSE(module["symbols"].empty());
  EXPECT_THAT(module.dump(), HasSubstr("app::Widget::name"));

  const auto explain = post_json(port, "/explain", {{"name", "app::Widget::name"}});
  EXPECT_EQ(explain.value("name", ""), "app::Widget::name");
  EXPECT_EQ(explain["completion"].value("content", ""), "Widget explanation");
  EXPECT_EQ(explain["prompt"].value("template_name", ""), "explain");
  EXPECT_EQ(explain["prompt"]["messages"].size(), 2U);
  EXPECT_THAT(explain["prompt"]["user_text"].get<std::string>(), HasSubstr("Request type"));

  const auto before_symbol_count = indexer.storage().graph().symbol_count();
  append_touch_function(repo_root / "app" / "widget.cpp", "qodeloc_api_touch", 42);

  const auto reindex =
      post_json(port, "/reindex", json{{"changed_files", json::array({"app/widget.cpp"})}});
  EXPECT_EQ(reindex["stats"].value("files_indexed", 0U), 1U);
  EXPECT_THAT(reindex.dump(), HasSubstr("app/widget.cpp"));
  EXPECT_TRUE(reindex["warnings"].empty());

  const auto after_symbol_count = indexer.storage().graph().symbol_count();
  EXPECT_EQ(after_symbol_count, before_symbol_count + 1U);

  const auto status_after = get_json(port, "/status");
  EXPECT_EQ(status_after.value("symbol_count", 0U), after_symbol_count);
  EXPECT_EQ(status_after.value("last_operation", ""), "update");

  api_server.stop();
}

} // namespace qodeloc::core
