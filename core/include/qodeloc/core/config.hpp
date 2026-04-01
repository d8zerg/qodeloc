#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/git_watcher.hpp>
#include <qodeloc/core/hierarchy.hpp>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/prompt_builder.hpp>
#include <qodeloc/core/retriever.hpp>
#include <string>

namespace qodeloc::core {

class Config final {
public:
  static Config load(const std::filesystem::path& env_file = {});
  static const Config& current();

  [[nodiscard]] const std::filesystem::path& env_file_path() const noexcept;
  [[nodiscard]] const std::filesystem::path& root_directory() const noexcept;

  [[nodiscard]] Embedder::Options embedder_options() const;
  [[nodiscard]] LlmClient::Options llm_options() const;
  [[nodiscard]] PromptBuilder::Options prompt_builder_options() const;
  [[nodiscard]] HierarchicalIndex::Options hierarchy_options() const;
  [[nodiscard]] Retriever::Options retriever_options() const;
  [[nodiscard]] Indexer::Options
  indexer_options(const std::filesystem::path& root_directory = {}) const;
  [[nodiscard]] GitWatcher::Options
  git_watcher_options(const std::filesystem::path& repository_root = {}) const;
  [[nodiscard]] std::filesystem::path storage_database_path() const;
  [[nodiscard]] std::string git_base_ref() const;

private:
  Config() = default;

  std::filesystem::path env_file_path_;
  std::filesystem::path root_directory_;
  Embedder::Options embedder_options_;
  LlmClient::Options llm_options_;
  PromptBuilder::Options prompt_builder_options_;
  HierarchicalIndex::Options hierarchy_options_;
  Retriever::Options retriever_options_;
  Indexer::Options indexer_options_;
  std::filesystem::path storage_database_path_;
  std::string git_base_ref_;
};

} // namespace qodeloc::core
