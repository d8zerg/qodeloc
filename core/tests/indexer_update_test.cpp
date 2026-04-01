#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qodeloc/core/indexer.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifndef QODELOC_TEST_REPOS_DIR
#define QODELOC_TEST_REPOS_DIR "."
#endif

namespace qodeloc::core {
namespace {

class TempWorkspace {
public:
  TempWorkspace()
      : root_(std::filesystem::temp_directory_path() /
              ("qodeloc-indexer-update-tests-" +
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

[[nodiscard]] std::filesystem::path fixture_repo_root() {
  return std::filesystem::path{QODELOC_TEST_REPOS_DIR} / "fmt";
}

void copy_fixture_item(const std::filesystem::path& source_root,
                       const std::filesystem::path& destination_root,
                       const std::filesystem::path& relative_path) {
  std::error_code ec;
  const auto source_path = source_root / relative_path;
  if (!std::filesystem::exists(source_path, ec)) {
    throw std::runtime_error("Missing fixture path: " + source_path.generic_string());
  }

  if (std::filesystem::is_directory(source_path, ec)) {
    std::filesystem::create_directories(destination_root / relative_path, ec);
    if (ec) {
      throw std::runtime_error("Failed to create directory: " +
                               (destination_root / relative_path).generic_string());
    }

    for (std::filesystem::recursive_directory_iterator it(source_path, ec), end; it != end; ++it) {
      const auto& entry = *it;
      const auto relative = std::filesystem::relative(entry.path(), source_root, ec);
      if (ec) {
        throw std::runtime_error("Failed to compute fixture path: " +
                                 entry.path().generic_string());
      }

      if (!relative.empty() && relative.begin()->generic_string() == ".git") {
        if (entry.is_directory()) {
          it.disable_recursion_pending();
        }
        continue;
      }

      const auto destination = destination_root / relative;
      if (entry.is_directory()) {
        ec.clear();
        std::filesystem::create_directories(destination, ec);
        if (ec) {
          throw std::runtime_error("Failed to create directory: " + destination.generic_string());
        }
        continue;
      }

      if (!entry.is_regular_file()) {
        continue;
      }

      ec.clear();
      std::filesystem::create_directories(destination.parent_path(), ec);
      if (ec) {
        throw std::runtime_error("Failed to create directory: " +
                                 destination.parent_path().generic_string());
      }

      ec.clear();
      std::filesystem::copy_file(entry.path(), destination,
                                 std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        throw std::runtime_error("Failed to copy fixture file: " + entry.path().generic_string());
      }
    }
    return;
  }

  const auto destination = destination_root / relative_path;
  ec.clear();
  std::filesystem::create_directories(destination.parent_path(), ec);
  if (ec) {
    throw std::runtime_error("Failed to create directory: " +
                             destination.parent_path().generic_string());
  }

  ec.clear();
  std::filesystem::copy_file(source_path, destination,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    throw std::runtime_error("Failed to copy fixture file: " + source_path.generic_string());
  }
}

void append_touch_function(const std::filesystem::path& file_path, std::string_view function_name,
                           int return_value) {
  std::ofstream output(file_path, std::ios::app);
  if (!output.is_open()) {
    throw std::runtime_error("Failed to open file for incremental update: " +
                             file_path.generic_string());
  }

  output << "\nstatic int " << function_name << "() { return " << return_value << "; }\n";
}

[[nodiscard]] std::size_t symbol_count_for_file(Indexer& indexer,
                                                const std::filesystem::path& file_path) {
  return indexer.storage().graph().symbols_for_file(file_path.generic_string()).size();
}

[[nodiscard]] bool has_symbol_name(const std::vector<StoredSymbol>& symbols,
                                   std::string_view qualified_name) {
  return std::any_of(symbols.begin(), symbols.end(), [qualified_name](const StoredSymbol& symbol) {
    return symbol.qualified_name == qualified_name;
  });
}

} // namespace

TEST(IndexerUpdateTest, UpdateRefreshesOnlyChangedFiles) {
  const auto source_fixture = fixture_repo_root();
  if (!std::filesystem::exists(source_fixture)) {
    GTEST_SKIP() << "Fixture repo is missing. Run make download-testdata-repo first.";
  }

  TempWorkspace workspace;
  const auto repo_root = workspace.root() / "fmt";
  copy_fixture_item(source_fixture, repo_root, "src/fmt-c.cc");
  copy_fixture_item(source_fixture, repo_root, "src/fmt.cc");
  copy_fixture_item(source_fixture, repo_root, "src/format.cc");
  copy_fixture_item(source_fixture, repo_root, "src/os.cc");
  copy_fixture_item(source_fixture, repo_root, "test/base-test.cc");
  copy_fixture_item(source_fixture, repo_root, "test/util.cc");
  copy_fixture_item(source_fixture, repo_root, "test/test-main.cc");

  const std::vector<std::filesystem::path> changed_files = {
      repo_root / "src/fmt-c.cc", repo_root / "src/format.cc", repo_root / "src/os.cc",
      repo_root / "test/base-test.cc", repo_root / "test/util.cc"};
  const std::filesystem::path untouched_file = repo_root / "test/test-main.cc";

  for (const auto& file_path : changed_files) {
    ASSERT_TRUE(std::filesystem::exists(file_path)) << file_path.generic_string();
  }
  ASSERT_TRUE(std::filesystem::exists(untouched_file));

  Indexer::Options options;
  options.root_directory = repo_root;
  options.embedding_batch_size = 8;
  options.recursive = true;

  std::vector<std::size_t> batch_sizes;
  Indexer indexer{options, {}, [&batch_sizes](std::span<const std::string> texts) {
                    batch_sizes.push_back(texts.size());
                    Embedder::Embeddings embeddings;
                    embeddings.reserve(texts.size());
                    for (const auto& text : texts) {
                      float seed = 1.0F;
                      for (const unsigned char ch : text) {
                        seed += static_cast<float>(ch % 17U) * 0.01F;
                      }
                      embeddings.push_back({seed, seed + 1.0F, seed + 2.0F, seed + 3.0F});
                    }
                    return embeddings;
                  }};

  ASSERT_TRUE(indexer.ready());

  const auto initial = indexer.index();
  EXPECT_EQ(initial.stats.parse_errors, 0U);
  EXPECT_EQ(initial.stats.files_indexed, 7U);
  EXPECT_GT(initial.stats.symbols_indexed, 20U);

  const auto baseline_total_symbols = indexer.storage().graph().symbol_count();
  std::unordered_map<std::string, std::size_t> baseline_symbol_counts;
  baseline_symbol_counts.reserve(changed_files.size() + 1);
  for (std::size_t index = 0; index < changed_files.size(); ++index) {
    baseline_symbol_counts.emplace(changed_files[index].generic_string(),
                                   symbol_count_for_file(indexer, changed_files[index]));
    append_touch_function(changed_files[index],
                          "qodeloc_incremental_touch_" + std::to_string(index + 1),
                          static_cast<int>(index + 1));
  }
  baseline_symbol_counts.emplace(untouched_file.generic_string(),
                                 symbol_count_for_file(indexer, untouched_file));

  const auto update_result = indexer.update(changed_files);
  EXPECT_EQ(update_result.stats.files_scanned, changed_files.size());
  EXPECT_EQ(update_result.stats.files_indexed, changed_files.size());
  EXPECT_EQ(update_result.stats.parse_errors, 0U);
  EXPECT_GE(update_result.stats.symbols_indexed, changed_files.size());
  EXPECT_FALSE(batch_sizes.empty());

  const auto updated_total_symbols = indexer.storage().graph().symbol_count();
  EXPECT_EQ(updated_total_symbols, baseline_total_symbols + changed_files.size());

  for (std::size_t index = 0; index < changed_files.size(); ++index) {
    const auto& file_path = changed_files[index];
    const auto symbols = indexer.storage().graph().symbols_for_file(file_path.generic_string());
    EXPECT_EQ(symbol_count_for_file(indexer, file_path),
              baseline_symbol_counts.at(file_path.generic_string()) + 1U)
        << file_path.generic_string();
    EXPECT_TRUE(has_symbol_name(symbols, "qodeloc_incremental_touch_" + std::to_string(index + 1)))
        << file_path.generic_string();
  }

  EXPECT_EQ(symbol_count_for_file(indexer, untouched_file),
            baseline_symbol_counts.at(untouched_file.generic_string()));
}

} // namespace qodeloc::core
