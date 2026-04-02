#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <qodeloc/core/module.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class LlmClient final : public IModule {
public:
  struct ChatMessage {
    std::string role;
    std::string content;
  };

  struct Options {
    std::string host;
    std::uint16_t port{};
    std::string api_path;
    std::string model;
    std::string api_key;
    std::size_t max_tokens{};
    std::chrono::milliseconds timeout{};
    std::size_t max_retries{};
    std::chrono::milliseconds initial_backoff{};
    std::chrono::milliseconds max_backoff{};
  };

  struct ChatRequest {
    std::vector<ChatMessage> messages;
    std::string model;
    bool stream{false};
    std::optional<float> temperature;
    std::optional<std::size_t> max_tokens;
    std::optional<float> top_p;
  };

  struct ChatResponse {
    std::string content;
    nlohmann::json raw;
  };

  using StreamCallback = std::function<bool(std::string_view)>;

  LlmClient();
  explicit LlmClient(Options options);

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] ChatResponse complete(const ChatRequest& request) const;
  [[nodiscard]] ChatResponse stream(const ChatRequest& request,
                                    const StreamCallback& on_chunk = {}) const;

private:
  [[nodiscard]] ChatResponse execute(const ChatRequest& request,
                                     const StreamCallback& on_chunk) const;
  [[nodiscard]] static nlohmann::json serialize_request(const ChatRequest& request,
                                                        std::string_view model);
  [[nodiscard]] static ChatResponse parse_completion_response(const std::string& body);
  [[nodiscard]] static bool is_retryable_status(unsigned int status) noexcept;

  Options options_;
};

} // namespace qodeloc::core
