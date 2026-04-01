#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/retriever.hpp>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qodeloc::core {

class PromptBuilder {
public:
  enum class RequestType {
    Search,
    Explain,
    Deps,
    Callers,
    Module,
  };

  struct LocalFile {
    std::filesystem::path path;
    std::string content;
  };

  struct Options {
    std::filesystem::path templates_directory;
    std::size_t context_token_limit{};
    std::size_t module_limit{};
    std::size_t symbol_limit{};
    std::size_t local_file_limit{};
    std::size_t module_token_limit{};
    std::size_t symbol_token_limit{};
    std::size_t local_file_token_limit{};
  };

  struct SectionBudget {
    std::size_t item_limit{};
    std::size_t token_limit{};
  };

  struct RenderedPrompt {
    std::string template_name;
    std::size_t context_token_limit{};
    std::size_t token_count{};
    std::string system_text;
    std::string user_text;
    std::vector<LlmClient::ChatMessage> messages;
  };

  PromptBuilder();
  explicit PromptBuilder(Options options);

  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] RenderedPrompt build(RequestType request_type, std::string_view query,
                                     const Retriever::Result& retrieval,
                                     std::span<const LocalFile> local_files = {}) const;
  [[nodiscard]] static std::string_view request_type_name(RequestType request_type) noexcept;

private:
  struct Template {
    std::string name;
    std::size_t context_token_limit{};
    std::string system;
    std::string user;
  };

  [[nodiscard]] Template load_template(RequestType request_type) const;
  [[nodiscard]] static std::string
  render_template(std::string_view template_text,
                  const std::unordered_map<std::string, std::string>& variables);
  [[nodiscard]] static std::size_t count_tokens(std::string_view text);
  [[nodiscard]] static std::string trim_to_token_limit(std::string text, std::size_t limit);
  [[nodiscard]] static std::string format_modules(const Retriever::Result& retrieval,
                                                  SectionBudget budget);
  [[nodiscard]] static std::string format_symbols(const Retriever::Result& retrieval,
                                                  SectionBudget budget);
  [[nodiscard]] static std::string format_local_files(std::span<const LocalFile> local_files,
                                                      SectionBudget budget);
  [[nodiscard]] static std::string format_symbol_context(const Retriever::SymbolContext& symbol,
                                                         std::size_t token_limit);
  [[nodiscard]] static std::string format_module_hit(const HierarchicalIndex::ModuleHit& module,
                                                     std::size_t token_limit);
  [[nodiscard]] static std::string format_local_file(const LocalFile& local_file,
                                                     std::size_t token_limit);
  [[nodiscard]] static std::string truncate_line(std::string_view text, std::size_t token_limit);
  [[nodiscard]] static std::string path_to_string(const std::filesystem::path& path);
  [[nodiscard]] static std::string clean_scalar(std::string_view text);

  Options options_;
  mutable std::unordered_map<std::string, Template> template_cache_;
  mutable std::mutex template_mutex_;
};

} // namespace qodeloc::core
