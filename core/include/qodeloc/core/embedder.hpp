#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <qodeloc/core/module.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qodeloc::core {

class Embedder final : public IModule {
public:
  using Embedding = std::vector<float>;
  using Embeddings = std::vector<Embedding>;

  struct Options {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::string api_path{"/v1/embeddings"};
    std::string model{"qodeloc-embedding"};
    std::size_t batch_size{8};
    std::chrono::milliseconds timeout{std::chrono::seconds{30}};
  };

  Embedder();
  explicit Embedder(Options options);

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] Embedding embed(std::string_view text) const;
  [[nodiscard]] Embeddings embed_batch(std::span<const std::string> texts) const;

private:
  [[nodiscard]] Embeddings request_batch(std::span<const std::string> texts) const;
  [[nodiscard]] static Embeddings parse_embeddings_response(const std::string& body,
                                                            std::size_t expected_count);

  Options options_;
};

} // namespace qodeloc::core
