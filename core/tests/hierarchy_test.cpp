#include <algorithm>
#include <array>
#include <cstddef>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/hierarchy.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {
namespace {

using ::testing::HasSubstr;

[[nodiscard]] Embedder::Embedding make_embedding(std::initializer_list<float> values) {
  return Embedder::Embedding(values.begin(), values.end());
}

[[nodiscard]] Indexer::IndexedSymbol make_symbol(std::string module_name, std::string module_path,
                                                 std::string file_path, std::string qualified_name,
                                                 SymbolKind kind, std::string signature,
                                                 Embedder::Embedding embedding) {
  Indexer::IndexedSymbol symbol;
  symbol.symbol_id = 0;
  symbol.symbol = StoredSymbol{std::move(file_path),
                               std::move(module_name),
                               std::move(module_path),
                               kind,
                               std::move(qualified_name),
                               std::move(signature),
                               1U,
                               1U};
  symbol.source_text =
      symbol.symbol.signature.empty() ? symbol.symbol.qualified_name : symbol.symbol.signature;
  symbol.embedding = std::move(embedding);
  return symbol;
}

[[nodiscard]] Embedder::Embedding query_embedding_for(std::string_view query) {
  if (query.contains("render") || query.contains("draw") || query.contains("paint") ||
      query.contains("canvas") || query.contains("frame") || query.contains("widget") ||
      query.contains("scene") || query.contains("shader") || query.contains("pipeline") ||
      query.contains("update") || query.contains("view") || query.contains("present") ||
      query.contains("redraw") || query.contains("buffer")) {
    return make_embedding({1.0F, 0.0F, 0.0F});
  }

  if (query.contains("socket") || query.contains("connect") || query.contains("network")) {
    return make_embedding({0.0F, 1.0F, 0.0F});
  }

  return make_embedding({0.0F, 0.0F, 1.0F});
}

[[nodiscard]] HierarchicalIndex::ModuleEmbeddingBatchFn module_embedding_batch() {
  return [](std::span<const std::string> summaries) {
    Embedder::Embeddings embeddings;
    embeddings.reserve(summaries.size());
    for (const auto& summary : summaries) {
      if (summary.contains("graphics")) {
        embeddings.push_back(make_embedding({1.0F, 0.0F, 0.0F}));
      } else if (summary.contains("network")) {
        embeddings.push_back(make_embedding({0.0F, 1.0F, 0.0F}));
      } else {
        embeddings.push_back(make_embedding({0.0F, 0.0F, 1.0F}));
      }
    }
    return embeddings;
  };
}

[[nodiscard]] const HierarchicalIndex::ModuleRecord*
find_module(const std::vector<HierarchicalIndex::ModuleRecord>& modules, std::string_view name) {
  const auto it = std::find_if(
      modules.begin(), modules.end(),
      [name](const HierarchicalIndex::ModuleRecord& module) { return module.module_name == name; });
  if (it == modules.end()) {
    return nullptr;
  }

  return &*it;
}

[[nodiscard]] std::vector<Indexer::IndexedSymbol> make_small_corpus() {
  std::vector<Indexer::IndexedSymbol> symbols;
  symbols.push_back(make_symbol("graphics", "graphics", "graphics/include/renderer.hpp",
                                "graphics::Renderer", SymbolKind::Class, "class Renderer",
                                make_embedding({0.92F, 0.08F, 0.0F})));
  symbols.push_back(make_symbol("graphics", "graphics", "graphics/src/renderer.cpp",
                                "graphics::Renderer::render_frame", SymbolKind::Method,
                                "void render_frame()", make_embedding({0.95F, 0.05F, 0.0F})));
  symbols.push_back(make_symbol("network", "network", "network/include/socket.hpp",
                                "network::Socket", SymbolKind::Class, "class Socket",
                                make_embedding({0.05F, 0.95F, 0.0F})));
  symbols.push_back(make_symbol("network", "network", "network/src/socket.cpp",
                                "network::Socket::connect", SymbolKind::Method, "void connect()",
                                make_embedding({0.02F, 0.98F, 0.0F})));
  return symbols;
}

[[nodiscard]] std::vector<Indexer::IndexedSymbol> make_large_corpus() {
  std::vector<Indexer::IndexedSymbol> symbols;
  symbols.reserve(12000);

  symbols.push_back(make_symbol("graphics", "graphics", "graphics/include/renderer.hpp",
                                "graphics::Renderer::render_frame", SymbolKind::Method,
                                "void render_frame()", make_embedding({0.95F, 0.05F, 0.0F})));
  for (std::size_t index = 0; index < 3999; ++index) {
    symbols.push_back(make_symbol(
        "graphics", "graphics", "graphics/src/graphics_helper_" + std::to_string(index) + ".cpp",
        "graphics::detail::helper_" + std::to_string(index), SymbolKind::Function, "void helper()",
        make_embedding({0.70F, 0.30F, 0.0F})));
  }

  for (std::size_t index = 0; index < 4000; ++index) {
    symbols.push_back(make_symbol(
        "network", "network", "network/src/network_helper_" + std::to_string(index) + ".cpp",
        "network::detail::helper_" + std::to_string(index), SymbolKind::Function, "void helper()",
        make_embedding({0.99F, 0.01F, 0.0F})));
  }

  for (std::size_t index = 0; index < 4000; ++index) {
    symbols.push_back(make_symbol(
        "storage", "storage", "storage/src/storage_helper_" + std::to_string(index) + ".cpp",
        "storage::detail::helper_" + std::to_string(index), SymbolKind::Function, "void helper()",
        make_embedding({0.98F, 0.02F, 0.0F})));
  }

  return symbols;
}

[[nodiscard]] double precision_at_five(const std::vector<HierarchicalIndex::SymbolHit>& hits,
                                       std::string_view expected_qualified_name) {
  const auto limit = std::min<std::size_t>(5, hits.size());
  const auto matches =
      std::count_if(hits.begin(), hits.begin() + static_cast<std::ptrdiff_t>(limit),
                    [expected_qualified_name](const HierarchicalIndex::SymbolHit& hit) {
                      return hit.symbol.symbol.qualified_name == expected_qualified_name;
                    });
  return static_cast<double>(matches) / 5.0;
}

} // namespace

TEST(HierarchicalIndexTest, BuildsReadableModuleSummaries) {
  HierarchicalIndex hierarchy;
  const auto symbols = make_small_corpus();
  hierarchy.build(symbols, module_embedding_batch());

  ASSERT_EQ(hierarchy.modules().size(), 2U);

  const auto* graphics = find_module(hierarchy.modules(), "graphics");
  ASSERT_NE(graphics, nullptr);
  EXPECT_EQ(graphics->header_count, 1U);
  EXPECT_EQ(graphics->public_symbol_count, 2U);
  EXPECT_THAT(graphics->summary, HasSubstr("module graphics"));
  EXPECT_THAT(graphics->summary, HasSubstr("headers 1"));
  EXPECT_THAT(graphics->summary, HasSubstr("graphics::Renderer"));
  EXPECT_THAT(graphics->summary, HasSubstr("graphics::Renderer::render_frame"));
}

TEST(HierarchicalIndexTest, HierarchicalSearchImprovesPrecisionAtFive) {
  auto options = Config::current().hierarchy_options();
  options.module_top_k = 1;
  options.symbol_top_k = 5;
  options.public_symbol_limit = 12;

  HierarchicalIndex hierarchy{options};
  const auto symbols = make_large_corpus();
  hierarchy.build(symbols, module_embedding_batch());

  ASSERT_EQ(hierarchy.modules().size(), 3U);

  constexpr std::array<std::string_view, 12> queries = {
      "render frame",  "draw canvas",     "paint widget",  "compose scene",
      "refresh frame", "shader pipeline", "update view",   "present widget",
      "redraw scene",  "frame buffer",    "render widget", "scene graph",
  };

  double hierarchical_precision = 0.0;
  double flat_precision = 0.0;
  for (const auto query : queries) {
    const auto query_embedding = query_embedding_for(query);
    const auto hierarchical = hierarchy.search(query_embedding);
    const auto flat = hierarchy.search_flat(query_embedding);
    hierarchical_precision +=
        precision_at_five(hierarchical.symbols, "graphics::Renderer::render_frame");
    flat_precision += precision_at_five(flat, "graphics::Renderer::render_frame");
  }

  hierarchical_precision /= static_cast<double>(queries.size());
  flat_precision /= static_cast<double>(queries.size());

  EXPECT_GT(hierarchical_precision, flat_precision);
  EXPECT_GT(hierarchical_precision, 0.15);
  EXPECT_LT(flat_precision, 0.05);
}

} // namespace qodeloc::core
