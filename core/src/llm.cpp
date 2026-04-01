#include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <cctype>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/llm.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace qodeloc::core {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

struct RequestError final : std::runtime_error {
  RequestError(const std::string& message, bool retryable_in) : std::runtime_error(message) {
    retryable = retryable_in;
  }

  bool retryable{false};
};

[[nodiscard]] std::string trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }

  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(begin, end - begin + 1)};
}

[[nodiscard]] std::string join_host_port(std::string_view host, std::uint16_t port) {
  std::ostringstream oss;
  oss << host << ':' << port;
  return oss.str();
}

[[nodiscard]] std::size_t find_event_delimiter(const std::string& buffer) {
  return buffer.find("\n\n");
}

[[nodiscard]] std::string extract_sse_payload(std::string_view event) {
  std::string payload;
  std::size_t pos = 0;
  while (pos <= event.size()) {
    const auto line_end = event.find('\n', pos);
    const auto raw_line = event.substr(
        pos, line_end == std::string_view::npos ? std::string_view::npos : line_end - pos);
    const auto line = trim(raw_line);
    if (line.rfind("data:", 0) == 0) {
      auto data = trim(std::string_view{line}.substr(5));
      if (data == "[DONE]") {
        return "[DONE]";
      }
      if (!payload.empty()) {
        payload.push_back('\n');
      }
      payload.append(data);
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    pos = line_end + 1;
  }

  return payload;
}

[[nodiscard]] std::string extract_delta_content(const nlohmann::json& json) {
  if (!json.contains("choices") || !json["choices"].is_array() || json["choices"].empty()) {
    return {};
  }

  const auto& choice = json["choices"].front();
  if (choice.contains("delta") && choice["delta"].is_object()) {
    return choice["delta"].value("content", std::string{});
  }
  if (choice.contains("message") && choice["message"].is_object()) {
    return choice["message"].value("content", std::string{});
  }
  if (choice.contains("text")) {
    return choice.value("text", std::string{});
  }
  return {};
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

LlmClient::LlmClient() : LlmClient(Config::current().llm_options()) {}

LlmClient::LlmClient(Options options) : options_(std::move(options)) {
  if (options_.host.empty()) {
    throw std::invalid_argument("LlmClient host must not be empty");
  }
  if (options_.port == 0) {
    throw std::invalid_argument("LlmClient port must be greater than zero");
  }
  if (options_.api_path.empty()) {
    throw std::invalid_argument("LlmClient API path must not be empty");
  }
  if (options_.model.empty()) {
    throw std::invalid_argument("LlmClient model must not be empty");
  }
  if (options_.timeout.count() <= 0) {
    throw std::invalid_argument("LlmClient timeout must be positive");
  }
  if (options_.initial_backoff.count() <= 0) {
    throw std::invalid_argument("LlmClient initial backoff must be positive");
  }
  if (options_.max_backoff.count() <= 0) {
    throw std::invalid_argument("LlmClient max backoff must be positive");
  }
  if (options_.max_backoff < options_.initial_backoff) {
    throw std::invalid_argument("LlmClient max backoff must be greater than initial backoff");
  }
}

std::string_view LlmClient::module_name() const noexcept {
  return "llm";
}

bool LlmClient::ready() const noexcept {
  return !options_.host.empty() && options_.port != 0 && !options_.api_path.empty() &&
         !options_.model.empty() && options_.timeout.count() > 0 &&
         options_.initial_backoff.count() > 0 && options_.max_backoff.count() > 0 &&
         options_.max_backoff >= options_.initial_backoff;
}

const LlmClient::Options& LlmClient::options() const noexcept {
  return options_;
}

LlmClient::ChatResponse LlmClient::complete(const ChatRequest& request) const {
  ChatRequest copy = request;
  copy.stream = false;
  return execute(copy, StreamCallback{});
}

LlmClient::ChatResponse LlmClient::stream(const ChatRequest& request,
                                          const StreamCallback& on_chunk) const {
  ChatRequest copy = request;
  copy.stream = true;
  return execute(copy, on_chunk);
}

LlmClient::ChatResponse LlmClient::execute(const ChatRequest& request,
                                           const StreamCallback& on_chunk) const {
  if (request.messages.empty()) {
    throw std::invalid_argument("LLM request must contain at least one message");
  }

  const auto model = request.model.empty() ? options_.model : request.model;
  const auto payload = serialize_request(request, model).dump();

  asio::io_context io;
  tcp::resolver resolver{io};
  beast::tcp_stream connection{io};

  auto backoff = options_.initial_backoff;
  for (std::size_t attempt = 0; attempt <= options_.max_retries; ++attempt) {
    try {
      beast::flat_buffer buffer;
      http::response_parser<http::string_body> parser;
      parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

      boost::system::error_code ec;
      const auto results = resolver.resolve(options_.host, std::to_string(options_.port), ec);
      if (ec) {
        throw RequestError("Failed to resolve LLM endpoint: " + ec.message(), true);
      }

      connection.expires_after(options_.timeout);
      connection.connect(results, ec);
      if (ec) {
        throw RequestError("Failed to connect to LLM endpoint: " + ec.message(), true);
      }

      http::request<http::string_body> http_request{http::verb::post, options_.api_path, 11};
      http_request.set(http::field::host, join_host_port(options_.host, options_.port));
      http_request.set(http::field::user_agent, "QodeLoc/0.1");
      http_request.set(http::field::content_type, "application/json");
      http_request.set(http::field::accept,
                       request.stream ? "text/event-stream" : "application/json");
      http_request.set(http::field::connection, "close");
      if (!options_.api_key.empty()) {
        http_request.set(http::field::authorization, "Bearer " + options_.api_key);
      }
      http_request.body() = payload;
      http_request.prepare_payload();

      connection.expires_after(options_.timeout);
      http::write(connection, http_request, ec);
      if (ec) {
        throw RequestError("Failed to send LLM request: " + ec.message(), true);
      }

      std::string assembled_content;
      nlohmann::json last_event = nlohmann::json::object();

      connection.expires_after(options_.timeout);
      http::read(connection, buffer, parser, ec);
      if (ec) {
        throw RequestError("Failed while reading LLM response: " + ec.message(),
                           ec != asio::error::operation_aborted);
      }

      const auto& response = parser.get();
      if (response.result_int() < 200 || response.result_int() >= 300) {
        const auto retryable =
            is_retryable_status(static_cast<unsigned int>(response.result_int()));
        std::ostringstream oss;
        oss << "LLM backend returned HTTP " << response.result_int();
        if (!response.body().empty()) {
          oss << ": " << response.body();
        }
        throw RequestError(oss.str(), retryable);
      }

      if (!request.stream) {
        auto parsed = parse_completion_response(response.body());
        if (parsed.raw.is_null()) {
          parsed.raw = nlohmann::json::parse(response.body());
        }
        close_stream(connection);
        return parsed;
      }

      std::string stream_buffer = response.body();
      while (true) {
        const auto delimiter = find_event_delimiter(stream_buffer);
        if (delimiter == std::string::npos) {
          break;
        }

        const auto event = stream_buffer.substr(0, delimiter);
        stream_buffer.erase(0, delimiter + 2);

        const auto payload_text = extract_sse_payload(event);
        if (payload_text.empty() || payload_text == "[DONE]") {
          continue;
        }

        const auto json = nlohmann::json::parse(payload_text);
        last_event = json;
        const auto chunk = extract_delta_content(json);
        if (!chunk.empty()) {
          assembled_content += chunk;
          if (on_chunk && !on_chunk(chunk)) {
            throw RequestError("LLM streaming callback aborted the response", false);
          }
        }
      }

      ChatResponse streamed;
      streamed.content = std::move(assembled_content);
      streamed.raw = last_event.is_null() ? nlohmann::json{{"content", streamed.content}}
                                          : std::move(last_event);
      close_stream(connection);
      return streamed;
    } catch (const RequestError& error) {
      close_stream(connection);

      if (!error.retryable || attempt == options_.max_retries) {
        throw;
      }

      std::this_thread::sleep_for(backoff);
      backoff = std::min(backoff * 2, options_.max_backoff);
      continue;
    } catch (...) {
      close_stream(connection);
      throw;
    }
  }

  throw std::runtime_error("LLM request failed after retries");
}

nlohmann::json LlmClient::serialize_request(const ChatRequest& request, std::string_view model) {
  nlohmann::json body;
  body["model"] = request.model.empty() ? std::string(model) : request.model;
  body["stream"] = request.stream;
  body["messages"] = nlohmann::json::array();

  for (const auto& message : request.messages) {
    body["messages"].push_back({{"role", message.role}, {"content", message.content}});
  }

  if (request.temperature.has_value()) {
    body["temperature"] = *request.temperature;
  }
  if (request.max_tokens.has_value()) {
    body["max_tokens"] = *request.max_tokens;
  }
  if (request.top_p.has_value()) {
    body["top_p"] = *request.top_p;
  }

  return body;
}

LlmClient::ChatResponse LlmClient::parse_completion_response(const std::string& body) {
  const auto json = nlohmann::json::parse(body);
  ChatResponse response;
  response.raw = json;
  response.content = extract_delta_content(json);
  if (response.content.empty()) {
    throw std::runtime_error("LLM response did not contain assistant content");
  }
  return response;
}

bool LlmClient::is_retryable_status(unsigned int status) noexcept {
  return status == 429 || (status >= 500 && status <= 599);
}

} // namespace qodeloc::core
