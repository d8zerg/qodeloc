#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/indexer.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace qodeloc::core {
namespace {

using ::testing::ElementsAre;

class TempWorkspace {
public:
  TempWorkspace()
      : root_(std::filesystem::temp_directory_path() /
              ("qodeloc-indexer-tests-" +
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

void write_source_file(const std::filesystem::path& file_path, std::string_view content) {
  std::filesystem::create_directories(file_path.parent_path());
  std::ofstream output(file_path);
  output << content;
}

[[nodiscard]] const Indexer::IndexedSymbol*
find_symbol(const std::vector<Indexer::IndexedSymbol>& symbols, std::string_view qualified_name) {
  const auto it =
      std::find_if(symbols.begin(), symbols.end(), [&](const Indexer::IndexedSymbol& symbol) {
        return symbol.symbol.qualified_name == qualified_name;
      });
  if (it == symbols.end()) {
    return nullptr;
  }

  return &*it;
}

} // namespace

TEST(IndexerTest, ReadyRequiresConfiguredRootDirectory) {
  Indexer indexer;

  EXPECT_FALSE(indexer.ready());
  EXPECT_EQ(indexer.module_name(), "indexer");
}

TEST(IndexerTest, IndexesRepositoryAndWritesSymbolGraph) {
  TempWorkspace workspace;
  const auto repo_root = workspace.root() / "repo";

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

  std::vector<std::size_t> batch_sizes;
  auto options = Config::current().indexer_options(repo_root);
  options.embedding_batch_size = 2;
  options.recursive = true;

  Indexer indexer{options, {}, [&batch_sizes](std::span<const std::string> texts) {
                    batch_sizes.push_back(texts.size());
                    Embedder::Embeddings embeddings;
                    embeddings.reserve(texts.size());
                    for (const auto& text : texts) {
                      const auto seed = static_cast<float>((text.size() % 7U) + 1U);
                      embeddings.push_back({seed, seed + 1.0F, seed + 2.0F});
                    }
                    return embeddings;
                  }};

  ASSERT_TRUE(indexer.ready());

  const auto result = indexer.index();

  EXPECT_EQ(result.stats.files_scanned, 3U);
  EXPECT_EQ(result.stats.files_indexed, 3U);
  EXPECT_EQ(result.stats.parse_errors, 0U);
  EXPECT_EQ(result.stats.symbols_indexed, 9U);
  EXPECT_EQ(result.stats.embedding_batches, batch_sizes.size());
  EXPECT_THAT(batch_sizes, ElementsAre(2U, 2U, 2U, 2U, 1U));
  EXPECT_EQ(result.symbols.size(), 9U);

  const auto* format_value = find_symbol(result.symbols, "math::format_value");
  ASSERT_NE(format_value, nullptr);
  const auto* widget_name = find_symbol(result.symbols, "app::Widget::name");
  ASSERT_NE(widget_name, nullptr);

  const auto callers = indexer.storage().graph().callers_of(format_value->symbol_id);
  ASSERT_EQ(callers.size(), 1U);
  EXPECT_EQ(callers.front().qualified_name, "app::Widget::name");
  EXPECT_EQ(callers.front().module_name, "app");

  const auto callees = indexer.storage().graph().callees_from(widget_name->symbol_id);
  ASSERT_EQ(callees.size(), 1U);
  EXPECT_EQ(callees.front().qualified_name, "math::format_value");
  EXPECT_EQ(callees.front().module_name, "math");

  const auto dependencies = indexer.storage().graph().transitive_module_dependencies("app", 2);
  ASSERT_EQ(dependencies.size(), 2U);
  EXPECT_THAT(dependencies, ElementsAre(ModuleDependency{"math", "math", 1U},
                                        ModuleDependency{"string", "string", 2U}));
}

} // namespace qodeloc::core
