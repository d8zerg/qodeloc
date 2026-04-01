#include <array>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/prompt_builder.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qodeloc::core {
namespace {

using ::testing::HasSubstr;

[[nodiscard]] Embedder::Embedding make_embedding(std::initializer_list<float> values) {
  return Embedder::Embedding(values.begin(), values.end());
}

[[nodiscard]] HierarchicalIndex::ModuleHit
make_module_hit(std::string module_name, std::string module_path, std::string summary, double score,
                std::size_t public_symbol_count, std::size_t header_count) {
  HierarchicalIndex::ModuleHit hit;
  hit.module.module_name = std::move(module_name);
  hit.module.module_path = std::move(module_path);
  hit.module.summary = std::move(summary);
  hit.module.public_symbol_count = public_symbol_count;
  hit.module.header_count = header_count;
  hit.module.embedding = make_embedding({1.0F, 0.0F, 0.0F});
  hit.score = score;
  return hit;
}

[[nodiscard]] Indexer::IndexedSymbol
make_indexed_symbol(std::string file_path, std::string module_name, std::string module_path,
                    std::string qualified_name, SymbolKind kind, std::string signature,
                    std::string source_text) {
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
  symbol.embedding = make_embedding({0.0F, 1.0F, 0.0F});
  return symbol;
}

[[nodiscard]] Retriever::SymbolContext
make_symbol_context(std::string file_path, std::string module_name, std::string module_path,
                    std::string qualified_name, std::string signature, std::string source_text,
                    std::string context, double score) {
  Retriever::SymbolContext symbol;
  symbol.symbol = make_indexed_symbol(
      std::move(file_path), std::move(module_name), std::move(module_path),
      std::move(qualified_name), SymbolKind::Method, std::move(signature), std::move(source_text));
  symbol.score = score;
  symbol.context = std::move(context);
  symbol.token_count = 32;
  return symbol;
}

[[nodiscard]] Retriever::Result make_retrieval() {
  Retriever::Result result;
  result.query = "render frame";
  result.query_embedding = make_embedding({0.5F, 0.5F, 0.0F});
  result.modules.push_back(
      make_module_hit("graphics", "graphics", "renderer and widget pipeline", 0.99, 4, 2));
  result.modules.push_back(
      make_module_hit("network", "network", "socket and transport helpers", 0.42, 2, 1));
  result.symbols.push_back(make_symbol_context(
      "graphics/include/renderer.hpp", "graphics", "graphics", "graphics::Renderer::render_frame",
      "void render_frame()", "void render_frame();",
      "Symbol: graphics::Renderer::render_frame\nModule: graphics\nSignature: void "
      "render_frame()\nSource: void render_frame();",
      0.97));
  result.symbols.push_back(make_symbol_context(
      "graphics/src/controller.cpp", "graphics", "graphics", "graphics::Controller::draw_frame",
      "void draw_frame()", "void draw_frame();",
      "Symbol: graphics::Controller::draw_frame\nModule: graphics\nSignature: void "
      "draw_frame()\nSource: void draw_frame();",
      0.88));
  return result;
}

[[nodiscard]] std::vector<PromptBuilder::LocalFile> make_local_files() {
  return {
      {std::filesystem::path{"src/main.cpp"}, "int main() {\n  return render_frame();\n}\n"},
      {std::filesystem::path{"include/renderer.hpp"},
       "class Renderer {\npublic:\n  void render_frame();\n};\n"},
  };
}

} // namespace

TEST(PromptBuilderTest, RendersExpectedSectionsForMultipleRequestTypes) {
  PromptBuilder builder;
  const auto retrieval = make_retrieval();
  const auto local_files = make_local_files();

  const std::array request_types{
      PromptBuilder::RequestType::Search, PromptBuilder::RequestType::Explain,
      PromptBuilder::RequestType::Deps,   PromptBuilder::RequestType::Callers,
      PromptBuilder::RequestType::Module,
  };
  const std::array queries{
      "render frame",
      "explain render_frame",
      "who calls draw_frame",
  };

  for (const auto request_type : request_types) {
    for (const auto query : queries) {
      const auto prompt = builder.build(request_type, query, retrieval, local_files);

      EXPECT_EQ(prompt.template_name, PromptBuilder::request_type_name(request_type));
      ASSERT_EQ(prompt.messages.size(), 2U);
      EXPECT_EQ(prompt.messages.front().role, "system");
      EXPECT_EQ(prompt.messages.back().role, "user");
      EXPECT_THAT(prompt.system_text, HasSubstr("QodeLoc"));
      EXPECT_THAT(prompt.user_text,
                  HasSubstr("Request type: " +
                            std::string(PromptBuilder::request_type_name(request_type))));
      EXPECT_THAT(prompt.user_text, HasSubstr("Query: " + std::string(query)));
      EXPECT_THAT(prompt.user_text, HasSubstr("Retrieved modules:"));
      EXPECT_THAT(prompt.user_text, HasSubstr("Retrieved symbols:"));
      EXPECT_THAT(prompt.user_text, HasSubstr("Local files:"));
      EXPECT_THAT(prompt.user_text, HasSubstr("graphics"));
      EXPECT_THAT(prompt.user_text, HasSubstr("render_frame"));
      EXPECT_THAT(prompt.user_text, HasSubstr("src/main.cpp"));
      EXPECT_LE(prompt.token_count, prompt.context_token_limit);
    }
  }
}

TEST(PromptBuilderTest, TruncatesLargeContextToLimit) {
  auto options = Config::current().prompt_builder_options();
  options.context_token_limit = 48;
  options.module_token_limit = 12;
  options.symbol_token_limit = 16;
  options.local_file_token_limit = 16;

  PromptBuilder builder{options};
  Retriever::Result retrieval = make_retrieval();
  retrieval.modules.front().module.summary =
      "This is a very long summary that should be truncated by the prompt builder to keep the "
      "prompt within the configured token budget.";
  retrieval.symbols.front().context =
      "Symbol: graphics::Renderer::render_frame\nModule: graphics\nSignature: void "
      "render_frame()\nSource: void render_frame();\n" +
      std::string(40, 'x');

  const std::vector<PromptBuilder::LocalFile> local_files = {
      {std::filesystem::path{"src/huge.cpp"},
       std::string(120, 'a') + "\n" + std::string(120, 'b') + "\n" + std::string(120, 'c')},
  };

  const auto prompt =
      builder.build(PromptBuilder::RequestType::Search, "render frame", retrieval, local_files);

  EXPECT_LE(prompt.token_count, prompt.context_token_limit);
  EXPECT_THAT(prompt.user_text, HasSubstr("Request type: search"));
  EXPECT_THAT(prompt.user_text, HasSubstr("Retrieved symbols:"));
}

} // namespace qodeloc::core
