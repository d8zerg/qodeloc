#include <algorithm>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qodeloc/core/parser.hpp>
#include <string>

#ifndef QODELOC_TESTDATA_DIR
#define QODELOC_TESTDATA_DIR "."
#endif

namespace qodeloc::core {
namespace {

using ::testing::ElementsAre;

[[nodiscard]] std::filesystem::path fixture_path(const char* file_name) {
  return std::filesystem::path{QODELOC_TESTDATA_DIR} / "parser" / file_name;
}

} // namespace

TEST(CppParserTest, ReadyReportsTrue) {
  const CppParser parser;

  EXPECT_TRUE(parser.ready());
  EXPECT_EQ(parser.module_name(), "parser");
}

TEST(CppParserTest, NestedNamespaces) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("nested_namespaces.cpp"));

  EXPECT_THAT(
      symbols,
      ElementsAre(Symbol{SymbolKind::Namespace, "outer", "", 1U, 13U},
                  Symbol{SymbolKind::Namespace, "outer::inner", "", 2U, 12U},
                  Symbol{SymbolKind::Enum, "outer::inner::Mode", "", 3U, 6U},
                  Symbol{SymbolKind::Class, "outer::inner::Widget", "", 7U, 10U},
                  Symbol{SymbolKind::Method, "outer::inner::Widget::run", "void run()", 9U, 9U},
                  Symbol{SymbolKind::Function, "outer::inner::helper", "void helper()", 11U, 11U}));
}

TEST(CppParserTest, Templates) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("templates.cpp"));

  EXPECT_THAT(
      symbols,
      ElementsAre(Symbol{SymbolKind::Namespace, "math", "", 1U, 15U},
                  Symbol{SymbolKind::Template, "math::identity", "template <typename T>", 2U, 4U},
                  Symbol{SymbolKind::Function, "math::identity", "T identity(T value)", 2U, 4U},
                  Symbol{SymbolKind::Template, "math::Box", "template <typename T>", 5U, 14U},
                  Symbol{SymbolKind::Class, "math::Box", "", 5U, 14U},
                  Symbol{SymbolKind::Method, "math::Box::Box", "explicit Box(T value) : value_(value)", 7U, 7U},
                  Symbol{SymbolKind::Method, "math::Box::get", "T get() const", 8U, 10U}));
}

TEST(CppParserTest, MultipleInheritance) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("inheritance.cpp"));

  EXPECT_THAT(symbols, ElementsAre(Symbol{SymbolKind::Namespace, "graphics", "", 1U, 18U},
                                   Symbol{SymbolKind::Class, "graphics::Drawable", "", 2U, 6U},
                                   Symbol{SymbolKind::Method, "graphics::Drawable::~Drawable",
                                          "virtual ~Drawable()", 4U, 4U},
                                   Symbol{SymbolKind::Method, "graphics::Drawable::draw",
                                          "virtual void draw() const", 5U, 5U},
                                   Symbol{SymbolKind::Class, "graphics::Movable", "", 7U, 11U},
                                   Symbol{SymbolKind::Method, "graphics::Movable::~Movable",
                                          "virtual ~Movable()", 9U, 9U},
                                   Symbol{SymbolKind::Method, "graphics::Movable::move",
                                          "virtual void move(int dx, int dy)", 10U, 10U},
                                   Symbol{SymbolKind::Class, "graphics::Sprite", "", 12U, 17U},
                                   Symbol{SymbolKind::Method, "graphics::Sprite::Sprite",
                                          "Sprite()", 14U, 14U},
                                   Symbol{SymbolKind::Method, "graphics::Sprite::draw",
                                          "void draw() const override", 15U, 15U},
                                   Symbol{SymbolKind::Method, "graphics::Sprite::move",
                                          "void move(int dx, int dy) override", 16U, 16U}));
}

TEST(CppParserTest, OperatorsAndLambdas) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("operators_lambdas.cpp"));

  EXPECT_THAT(
      symbols,
      ElementsAre(
          Symbol{SymbolKind::Namespace, "util", "", 1U, 18U},
          Symbol{SymbolKind::Class, "util::Counter", "", 2U, 7U},
          Symbol{SymbolKind::Method, "util::Counter::Counter", "Counter()", 4U, 4U},
          Symbol{SymbolKind::Method, "util::Counter::operator++", "Counter& operator++()", 5U, 5U},
          Symbol{SymbolKind::Method, "util::Counter::operator bool",
                 "explicit operator bool() const", 6U, 6U},
          Symbol{SymbolKind::Method, "util::Counter::operator++", "Counter& Counter::operator++()",
                 8U, 10U},
          Symbol{SymbolKind::Method, "util::Counter::operator bool",
                 "Counter::operator bool() const", 11U, 13U},
          Symbol{SymbolKind::Function, "util::use_lambda", "void use_lambda()", 14U, 17U}));
}

TEST(CppParserTest, VariadicTemplates) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("variadic_templates.cpp"));

  EXPECT_THAT(symbols, ElementsAre(Symbol{SymbolKind::Namespace, "variadic", "", 1U, 17U},
                                   Symbol{SymbolKind::Template, "variadic::log_all",
                                          "template <typename... Ts>", 2U, 4U},
                                   Symbol{SymbolKind::Function, "variadic::log_all",
                                          "void log_all(Ts&&... values)", 2U, 4U},
                                   Symbol{SymbolKind::Template, "variadic::Pack",
                                          "template <typename T, typename... Rest>", 5U, 16U},
                                   Symbol{SymbolKind::Class, "variadic::Pack", "", 5U, 16U},
                                   Symbol{SymbolKind::Method, "variadic::Pack::Pack",
                                          "Pack(T first, Rest... rest) : first_(first)", 7U, 9U},
                                   Symbol{SymbolKind::Method, "variadic::Pack::size",
                                          "std::size_t size() const", 10U, 12U}));
}

} // namespace qodeloc::core
