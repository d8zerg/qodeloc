#include <algorithm>
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <qodeloc/core/parser.hpp>
#include <string>
#include <vector>

#ifndef QODELOC_TESTDATA_DIR
#define QODELOC_TESTDATA_DIR "."
#endif

namespace qodeloc::core {
namespace {

using ::testing::ElementsAre;

[[nodiscard]] std::filesystem::path fixture_path(const char* file_name) {
  return std::filesystem::path{QODELOC_TESTDATA_DIR} / "parser" / file_name;
}

[[nodiscard]] std::vector<std::string> string_list(std::initializer_list<const char*> values) {
  return std::vector<std::string>{values.begin(), values.end()};
}

[[nodiscard]] SymbolDependencies
make_dependencies(std::initializer_list<const char*> includes = {},
                  std::initializer_list<const char*> outgoing_calls = {},
                  std::initializer_list<const char*> base_classes = {}) {
  return SymbolDependencies{string_list(includes), string_list(outgoing_calls),
                            string_list(base_classes)};
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
                  Symbol{SymbolKind::Method, "math::Box::Box",
                         "explicit Box(T value) : value_(value)", 7U, 7U},
                  Symbol{SymbolKind::Method, "math::Box::get", "T get() const", 8U, 10U}));
}

TEST(CppParserTest, MultipleInheritance) {
  const CppParser parser;
  const auto symbols = parser.parse_file(fixture_path("inheritance.cpp"));

  EXPECT_THAT(
      symbols,
      ElementsAre(
          Symbol{SymbolKind::Namespace, "graphics", "", 1U, 18U},
          Symbol{SymbolKind::Class, "graphics::Drawable", "", 2U, 6U},
          Symbol{SymbolKind::Method, "graphics::Drawable::~Drawable", "virtual ~Drawable()", 4U,
                 4U},
          Symbol{SymbolKind::Method, "graphics::Drawable::draw", "virtual void draw() const", 5U,
                 5U},
          Symbol{SymbolKind::Class, "graphics::Movable", "", 7U, 11U},
          Symbol{SymbolKind::Method, "graphics::Movable::~Movable", "virtual ~Movable()", 9U, 9U},
          Symbol{SymbolKind::Method, "graphics::Movable::move", "virtual void move(int dx, int dy)",
                 10U, 10U},
          Symbol{SymbolKind::Class, "graphics::Sprite", "", 12U, 17U,
                 make_dependencies({}, {}, {"graphics::Drawable", "graphics::Movable"})},
          Symbol{SymbolKind::Method, "graphics::Sprite::Sprite", "Sprite()", 14U, 14U},
          Symbol{SymbolKind::Method, "graphics::Sprite::draw", "void draw() const override", 15U,
                 15U},
          Symbol{SymbolKind::Method, "graphics::Sprite::move", "void move(int dx, int dy) override",
                 16U, 16U}));
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

TEST(CppParserTest, Dependencies) {
  const CppParser parser;

  const auto header_symbols = parser.parse_file(fixture_path("dependencies/common.hpp"));
  EXPECT_THAT(header_symbols, ElementsAre(Symbol{SymbolKind::Namespace, "deps", "", 6U, 16U,
                                                 make_dependencies({"string", "vector"})},
                                          Symbol{SymbolKind::Function, "deps::format_label",
                                                 "std::string format_label(int value)", 8U, 8U,
                                                 make_dependencies({"string", "vector"})},
                                          Symbol{SymbolKind::Function, "deps::duplicate_label",
                                                 "std::string duplicate_label(int value)", 9U, 9U,
                                                 make_dependencies({"string", "vector"})},
                                          Symbol{SymbolKind::Class, "deps::Base", "", 11U, 14U,
                                                 make_dependencies({"string", "vector"})},
                                          Symbol{SymbolKind::Method, "deps::Base::kind",
                                                 "virtual std::string kind() const = 0", 13U, 13U,
                                                 make_dependencies({"string", "vector"})}));

  const auto common_symbols = parser.parse_file(fixture_path("dependencies/common.cpp"));
  EXPECT_THAT(
      common_symbols,
      ElementsAre(
          Symbol{SymbolKind::Namespace, "deps", "", 3U, 13U, make_dependencies({"common.hpp"})},
          Symbol{SymbolKind::Function, "deps::format_label", "std::string format_label(int value)",
                 5U, 7U, make_dependencies({"common.hpp"}, {"std::to_string"})},
          Symbol{SymbolKind::Function, "deps::duplicate_label",
                 "std::string duplicate_label(int value)", 9U, 11U,
                 make_dependencies({"common.hpp"}, {"format_label"})}));

  const auto derived_symbols = parser.parse_file(fixture_path("dependencies/derived.cpp"));
  EXPECT_THAT(
      derived_symbols,
      ElementsAre(
          Symbol{SymbolKind::Namespace, "deps", "", 5U, 21U,
                 make_dependencies({"common.hpp", "utility"})},
          Symbol{SymbolKind::Class, "deps::Derived", "", 7U, 19U,
                 make_dependencies({"common.hpp", "utility"}, {}, {"deps::Base"})},
          Symbol{SymbolKind::Method, "deps::Derived::kind", "std::string kind() const override", 9U,
                 11U, make_dependencies({"common.hpp", "utility"}, {"duplicate_label"})},
          Symbol{SymbolKind::Method, "deps::Derived::decorate", "std::string decorate() const", 13U,
                 15U,
                 make_dependencies({"common.hpp", "utility"}, {"format_label", "std::move"})}));
}

} // namespace qodeloc::core
