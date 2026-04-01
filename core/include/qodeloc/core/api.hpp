#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/prompt_builder.hpp>
#include <qodeloc/core/retriever.hpp>
#include <string>
#include <string_view>

namespace qodeloc::core {

class ApiServer final : public IModule {
public:
  struct Options {
    std::string host;
    std::uint16_t port{};
    std::size_t max_body_bytes{};
    std::chrono::milliseconds request_timeout{};
  };

  struct Status {
    bool running{};
    std::string host;
    std::uint16_t port{};
    std::filesystem::path root_directory;
    std::size_t symbol_count{};
    std::size_t module_count{};
    std::size_t indexed_files{};
    Indexer::Stats last_stats;
    std::chrono::system_clock::time_point last_indexed_at{};
    std::string last_operation;
    bool retriever_ready{};
    bool llm_ready{};
  };

  using ModuleEmbeddingBatchFn = Retriever::ModuleEmbeddingBatchFn;

  ApiServer();
  explicit ApiServer(Options options);
  ~ApiServer() override;

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint16_t bound_port() const noexcept;
  [[nodiscard]] Status status() const;

  void attach_indexer(Indexer& indexer) noexcept;
  void attach_retriever(Retriever& retriever) noexcept;
  void attach_prompt_builder(PromptBuilder& prompt_builder) noexcept;
  void attach_llm_client(LlmClient& llm_client) noexcept;
  void attach_module_embedding_batch(ModuleEmbeddingBatchFn module_embedding_batch);

  void start();
  void stop() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace qodeloc::core
