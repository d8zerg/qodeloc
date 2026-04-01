#include <algorithm>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <qodeloc/core/retriever.hpp>
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
                                                 std::string source_text,
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
  symbol.source_text = std::move(source_text);
  symbol.embedding = std::move(embedding);
  return symbol;
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

[[nodiscard]] Retriever::QueryEmbeddingFn query_embedding_fn() {
  return [](std::string_view query) {
    if (query.contains("render") || query.contains("draw") || query.contains("widget") ||
        query.contains("scene") || query.contains("frame")) {
      return make_embedding({1.0F, 0.0F, 0.0F});
    }

    if (query.contains("socket") || query.contains("network") || query.contains("connect")) {
      return make_embedding({0.0F, 1.0F, 0.0F});
    }

    return make_embedding({0.0F, 0.0F, 1.0F});
  };
}

} // namespace

TEST(RetrieverTest, ReadyRequiresBuiltCorpusAndStorage) {
  Retriever retriever;

  EXPECT_FALSE(retriever.ready());
  EXPECT_EQ(retriever.module_name(), "retriever");
}

TEST(RetrieverTest, RetrievesSymbolContextFromHierarchyAndGraph) {
  Storage storage;
  ASSERT_TRUE(storage.ready());

  std::vector<Indexer::IndexedSymbol> symbols;
  symbols.push_back(make_symbol("graphics", "graphics", "graphics/include/renderer.hpp",
                                "graphics::Renderer::render_frame", SymbolKind::Method,
                                "void render_frame()", "void render_frame() { draw(); present(); }",
                                make_embedding({0.99F, 0.01F, 0.0F})));
  symbols.push_back(make_symbol("graphics", "graphics", "graphics/src/controller.cpp",
                                "graphics::Controller::draw_frame", SymbolKind::Method,
                                "void draw_frame()", "void draw_frame() { render_frame(); }",
                                make_embedding({0.95F, 0.05F, 0.0F})));
  symbols.push_back(make_symbol("graphics", "graphics", "graphics/src/renderer.cpp",
                                "graphics::Renderer::present", SymbolKind::Method, "void present()",
                                "void present() {}", make_embedding({0.90F, 0.10F, 0.0F})));
  symbols.push_back(make_symbol("network", "network", "network/src/socket.cpp",
                                "network::Socket::connect", SymbolKind::Method, "void connect()",
                                "void connect() {}", make_embedding({0.0F, 1.0F, 0.0F})));

  std::vector<StoredSymbol> stored_symbols;
  stored_symbols.reserve(symbols.size());
  for (const auto& symbol : symbols) {
    stored_symbols.push_back(symbol.symbol);
  }

  const auto ids = storage.graph().write_symbols(stored_symbols);
  ASSERT_EQ(ids.size(), symbols.size());
  for (std::size_t index = 0; index < symbols.size(); ++index) {
    symbols[index].symbol_id = ids[index];
  }

  std::vector<CallEdge> call_edges = {
      {symbols[1].symbol_id, symbols[0].symbol_id},
      {symbols[0].symbol_id, symbols[2].symbol_id},
  };
  storage.graph().write_calls(call_edges);

  Retriever::Options options;
  options.hierarchy.module_top_k = 1;
  options.hierarchy.symbol_top_k = 1;
  options.hierarchy.public_symbol_limit = 4;
  options.related_symbol_limit = 4;
  options.context_token_limit = 64;

  Retriever retriever{options, query_embedding_fn()};
  retriever.attach_storage(storage);
  retriever.build(symbols, module_embedding_batch());

  ASSERT_TRUE(retriever.ready());

  const auto result = retriever.retrieve("render frame");
  EXPECT_EQ(result.query, "render frame");
  ASSERT_EQ(result.modules.size(), 1U);
  EXPECT_EQ(result.modules.front().module.module_name, "graphics");
  ASSERT_EQ(result.symbols.size(), 1U);

  const auto& top_symbol = result.symbols.front();
  EXPECT_EQ(top_symbol.symbol.symbol.qualified_name, "graphics::Renderer::render_frame");
  EXPECT_THAT(top_symbol.context, HasSubstr("Symbol: graphics::Renderer::render_frame"));
  EXPECT_THAT(top_symbol.context, HasSubstr("Callers:"));
  EXPECT_THAT(top_symbol.context, HasSubstr("graphics::Controller::draw_frame"));
  EXPECT_THAT(top_symbol.context, HasSubstr("Callees:"));
  EXPECT_THAT(top_symbol.context, HasSubstr("graphics::Renderer::present"));
  EXPECT_LE(top_symbol.token_count, options.context_token_limit);
}

} // namespace qodeloc::core
