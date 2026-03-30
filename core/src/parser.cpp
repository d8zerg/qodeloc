#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <qodeloc/core/parser.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-cpp.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qodeloc::core {

namespace {

struct ScopeContext {
  std::vector<std::string> scopes;
  std::size_t class_depth{0};
};

struct ParserDeleter {
  void operator()(TSParser* parser) const noexcept {
    ts_parser_delete(parser);
  }
};

struct TreeDeleter {
  void operator()(TSTree* tree) const noexcept {
    ts_tree_delete(tree);
  }
};

using ParserHandle = std::unique_ptr<TSParser, ParserDeleter>;
using TreeHandle = std::unique_ptr<TSTree, TreeDeleter>;

[[nodiscard]] std::string_view trim_view(std::string_view text) noexcept {
  const auto begin = text.find_first_not_of(" \t\n\r\f\v");
  if (begin == std::string_view::npos) {
    return {};
  }

  const auto end = text.find_last_not_of(" \t\n\r\f\v");
  return text.substr(begin, end - begin + 1);
}

[[nodiscard]] TSNode child_by_field(TSNode node, const char* field_name) noexcept {
  return ts_node_child_by_field_name(node, field_name, std::strlen(field_name));
}

[[nodiscard]] std::string_view source_slice(std::string_view source, TSNode node) noexcept {
  const auto start = ts_node_start_byte(node);
  const auto end = ts_node_end_byte(node);
  return source.substr(start, end - start);
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  return std::string{trim_view(text)};
}

[[nodiscard]] std::string normalize_segment(std::string_view segment) {
  segment = trim_view(segment);
  if (segment.empty()) {
    return {};
  }

  if (segment.starts_with("operator") || segment.starts_with("~")) {
    return std::string{segment};
  }

  std::string normalized;
  normalized.reserve(segment.size());

  std::size_t template_depth = 0;
  for (const char ch : segment) {
    if (ch == '<') {
      ++template_depth;
      continue;
    }

    if (ch == '>' && template_depth > 0) {
      --template_depth;
      continue;
    }

    if (template_depth == 0) {
      normalized.push_back(ch);
    }
  }

  return trim_copy(normalized);
}

[[nodiscard]] std::string normalize_qualified_name(std::string_view text) {
  text = trim_view(text);
  if (text.empty()) {
    return {};
  }

  std::string normalized;
  std::string segment;
  std::size_t template_depth = 0;

  auto flush_segment = [&]() {
    const std::string current = normalize_segment(segment);
    if (!current.empty()) {
      if (!normalized.empty()) {
        normalized.append("::");
      }
      normalized.append(current);
    }
    segment.clear();
  };

  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i + 1 < text.size() && text[i] == ':' && text[i + 1] == ':' && template_depth == 0) {
      flush_segment();
      ++i;
      continue;
    }

    const char ch = text[i];
    if (ch == '<') {
      ++template_depth;
    } else if (ch == '>' && template_depth > 0) {
      --template_depth;
    }

    segment.push_back(ch);
  }

  flush_segment();

  if (normalized.empty()) {
    return std::string{trim_view(text)};
  }

  return normalized;
}

[[nodiscard]] std::string join_scopes(const std::vector<std::string>& scopes) {
  std::string joined;
  for (const auto& scope : scopes) {
    if (scope.empty()) {
      continue;
    }

    if (!joined.empty()) {
      joined.append("::");
    }
    joined.append(scope);
  }
  return joined;
}

[[nodiscard]] std::string qualify_name(const ScopeContext& ctx, std::string_view leaf) {
  const std::string normalized_leaf = normalize_qualified_name(leaf);
  if (normalized_leaf.empty()) {
    return "(anonymous)";
  }

  const std::string scope = join_scopes(ctx.scopes);
  if (scope.empty()) {
    return normalized_leaf;
  }

  const std::string scope_prefix = scope + "::";
  if (normalized_leaf == scope || normalized_leaf.starts_with(scope_prefix)) {
    return normalized_leaf;
  }

  return scope_prefix + normalized_leaf;
}

[[nodiscard]] std::string trim_signature(std::string text) {
  while (!text.empty() && text.back() == ';') {
    text.pop_back();
  }

  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.pop_back();
  }

  return text;
}

[[nodiscard]] std::string signature_from_range(std::string_view source, std::uint32_t start,
                                               std::uint32_t end) {
  if (start >= end || start >= source.size()) {
    return {};
  }

  const std::size_t bounded_end = std::min<std::size_t>(end, source.size());
  return trim_signature(std::string{trim_view(source.substr(start, bounded_end - start))});
}

[[nodiscard]] std::string signature_for_node(TSNode node, std::string_view source,
                                             TSNode cutoff = TSNode{}) {
  const auto start = ts_node_start_byte(node);
  const auto end = ts_node_is_null(cutoff) ? ts_node_end_byte(node) : ts_node_start_byte(cutoff);
  return signature_from_range(source, start, end);
}

[[nodiscard]] bool has_ancestor_type(TSNode node, std::string_view ancestor_type) {
  for (TSNode current = ts_node_parent(node); !ts_node_is_null(current);
       current = ts_node_parent(current)) {
    if (std::string_view{ts_node_type(current)} == ancestor_type) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool is_namespace_node(std::string_view type) {
  return type == "namespace_definition";
}

[[nodiscard]] bool is_class_node(std::string_view type) {
  return type == "class_specifier" || type == "struct_specifier" || type == "union_specifier";
}

[[nodiscard]] bool is_function_definition_node(std::string_view type) {
  return type == "function_definition" || type == "inline_method_definition" ||
         type == "constructor_or_destructor_definition" || type == "operator_cast_definition";
}

[[nodiscard]] bool is_function_like_declaration_node(std::string_view type) {
  return type == "field_declaration" || type == "declaration" ||
         type == "constructor_or_destructor_declaration" || type == "operator_cast_declaration";
}

[[nodiscard]] TSNode unwrap_reference_declarator(TSNode node) noexcept {
  while (!ts_node_is_null(node) && std::string_view{ts_node_type(node)} == "reference_declarator" &&
         ts_node_named_child_count(node) == 1) {
    node = ts_node_named_child(node, 0);
  }

  return node;
}

[[nodiscard]] TSNode declarator_name_node(TSNode node) noexcept {
  if (ts_node_is_null(node)) {
    return {};
  }

  const std::string_view type{ts_node_type(node)};
  if (type == "operator_cast") {
    return node;
  }

  if (type == "reference_declarator") {
    return declarator_name_node(unwrap_reference_declarator(node));
  }

  if (type == "function_declarator" || type == "function_field_declarator" ||
      type == "constructor_or_destructor_definition" ||
      type == "constructor_or_destructor_declaration") {
    TSNode name_node = child_by_field(node, "declarator");
    if (ts_node_is_null(name_node)) {
      return node;
    }

    return declarator_name_node(name_node);
  }

  return node;
}

[[nodiscard]] TSNode function_like_name_node(TSNode node) noexcept {
  TSNode declarator = child_by_field(node, "declarator");
  if (ts_node_is_null(declarator)) {
    return {};
  }

  const std::string_view declarator_type{ts_node_type(declarator)};
  if (declarator_type == "operator_cast") {
    TSNode inner = child_by_field(declarator, "declarator");
    if (!ts_node_is_null(inner)) {
      return declarator;
    }
  }

  return declarator_name_node(declarator);
}

[[nodiscard]] std::string function_like_name(TSNode node, std::string_view source) {
  const TSNode name_node = function_like_name_node(node);
  if (ts_node_is_null(name_node)) {
    return {};
  }

  if (std::string_view{ts_node_type(name_node)} == "qualified_identifier") {
    std::string normalized = normalize_qualified_name(source_slice(source, name_node));
    const auto paren = normalized.find('(');
    if (paren != std::string::npos) {
      normalized.erase(paren);
      while (!normalized.empty() &&
             std::isspace(static_cast<unsigned char>(normalized.back())) != 0) {
        normalized.pop_back();
      }
    }
    return normalized;
  }

  if (std::string_view{ts_node_type(name_node)} == "operator_cast") {
    const TSNode inner = child_by_field(name_node, "declarator");
    if (!ts_node_is_null(inner)) {
      const auto start = ts_node_start_byte(name_node);
      const auto end = ts_node_start_byte(inner);
      std::string normalized = normalize_qualified_name(source.substr(start, end - start));
      const auto paren = normalized.find('(');
      if (paren != std::string::npos) {
        normalized.erase(paren);
        while (!normalized.empty() &&
               std::isspace(static_cast<unsigned char>(normalized.back())) != 0) {
          normalized.pop_back();
        }
      }
      return normalized;
    }
  }

  return normalize_qualified_name(source_slice(source, name_node));
}

[[nodiscard]] std::string symbol_name_from_node(TSNode node, std::string_view source) {
  const std::string_view type{ts_node_type(node)};

  if (type == "namespace_definition" || type == "class_specifier" || type == "struct_specifier" ||
      type == "union_specifier" || type == "enum_specifier") {
    TSNode name_node = child_by_field(node, "name");
    if (ts_node_is_null(name_node)) {
      return "(anonymous)";
    }

    return normalize_qualified_name(source_slice(source, name_node));
  }

  if (type == "template_declaration") {
    const auto named_child_count = ts_node_named_child_count(node);
    if (named_child_count == 0) {
      return "(anonymous)";
    }

    return symbol_name_from_node(ts_node_named_child(node, named_child_count - 1), source);
  }

  if (is_function_definition_node(type) || is_function_like_declaration_node(type)) {
    return function_like_name(node, source);
  }

  return {};
}

[[nodiscard]] bool is_method_like_context(const ScopeContext& ctx, TSNode node,
                                          std::string_view name_text,
                                          const std::unordered_set<std::string>& known_classes) {
  if (has_ancestor_type(node, "friend_declaration")) {
    return false;
  }

  if (ctx.class_depth > 0) {
    return true;
  }

  const auto separator = name_text.rfind("::");
  if (separator == std::string_view::npos) {
    return false;
  }

  const std::string prefix = normalize_qualified_name(name_text.substr(0, separator));
  if (prefix.empty()) {
    return false;
  }

  const std::string qualified_prefix = qualify_name(ctx, prefix);
  return known_classes.contains(qualified_prefix);
}

class SymbolCollector {
public:
  explicit SymbolCollector(std::string_view source) : source_{source} {}

  [[nodiscard]] CppParser::Symbols collect(TSNode root) {
    CppParser::Symbols symbols;
    walk(root, ScopeContext{}, symbols);
    return symbols;
  }

private:
  void emit_symbol(CppParser::Symbols& symbols, SymbolKind kind, const ScopeContext& ctx,
                   TSNode node, std::string_view name_text, std::string signature = {}) {
    Symbol symbol{};
    symbol.kind = kind;
    symbol.qualified_name = qualify_name(ctx, name_text);
    symbol.signature = std::move(signature);
    symbol.start_line = ts_node_start_point(node).row + 1U;
    symbol.end_line = ts_node_end_point(node).row + 1U;
    if (symbol.kind == SymbolKind::Class) {
      known_classes_.insert(symbol.qualified_name);
    }

    symbols.push_back(std::move(symbol));
  }

  void walk_named_children(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const auto named_child_count = ts_node_named_child_count(node);
    for (std::uint32_t i = 0; i < named_child_count; ++i) {
      walk(ts_node_named_child(node, i), ctx, symbols);
    }
  }

  void emit_namespace(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const std::string name = symbol_name_from_node(node, source_);
    emit_symbol(symbols, SymbolKind::Namespace, ctx, node, name);

    ScopeContext child_ctx = std::move(ctx);
    child_ctx.scopes.push_back(name);
    walk_named_children(node, std::move(child_ctx), symbols);
  }

  void emit_class(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const std::string name = symbol_name_from_node(node, source_);
    emit_symbol(symbols, SymbolKind::Class, ctx, node, name);

    ScopeContext child_ctx = std::move(ctx);
    child_ctx.scopes.push_back(name);
    child_ctx.class_depth += 1;
    walk_named_children(node, std::move(child_ctx), symbols);
  }

  void emit_enum(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const std::string name = symbol_name_from_node(node, source_);
    emit_symbol(symbols, SymbolKind::Enum, ctx, node, name);
    walk_named_children(node, std::move(ctx), symbols);
  }

  void emit_template(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const auto named_child_count = ts_node_named_child_count(node);
    if (named_child_count == 0) {
      emit_symbol(symbols, SymbolKind::Template, ctx, node, "(anonymous)",
                  signature_for_node(node, source_));
      return;
    }

    const TSNode body_node = ts_node_named_child(node, named_child_count - 1);
    const std::string name = symbol_name_from_node(body_node, source_);
    emit_symbol(symbols, SymbolKind::Template, ctx, node, name,
                signature_for_node(node, source_, body_node));
    walk_named_children(node, std::move(ctx), symbols);
  }

  void emit_function_like(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    const std::string_view type{ts_node_type(node)};
    TSNode declarator = child_by_field(node, "declarator");

    if (ts_node_is_null(declarator)) {
      walk_named_children(node, std::move(ctx), symbols);
      return;
    }

    const std::string name = function_like_name(node, source_);
    if (name.empty()) {
      walk_named_children(node, std::move(ctx), symbols);
      return;
    }

    const bool method_like = is_method_like_context(ctx, node, name, known_classes_);

    const SymbolKind kind = method_like ? SymbolKind::Method : SymbolKind::Function;
    const TSNode body_node = child_by_field(node, "body");
    const std::string signature = ts_node_is_null(body_node)
                                      ? signature_for_node(node, source_)
                                      : signature_for_node(node, source_, body_node);

    emit_symbol(symbols, kind, ctx, node, name, signature);
    walk_named_children(node, std::move(ctx), symbols);
  }

  void walk(TSNode node, ScopeContext ctx, CppParser::Symbols& symbols) {
    if (ts_node_is_null(node) || ts_node_is_missing(node) || ts_node_is_extra(node) ||
        ts_node_is_error(node)) {
      return;
    }

    const std::string_view type{ts_node_type(node)};

    if (type == "template_declaration") {
      emit_template(node, std::move(ctx), symbols);
      return;
    }

    if (is_namespace_node(type)) {
      emit_namespace(node, std::move(ctx), symbols);
      return;
    }

    if (is_class_node(type)) {
      emit_class(node, std::move(ctx), symbols);
      return;
    }

    if (type == "enum_specifier") {
      emit_enum(node, std::move(ctx), symbols);
      return;
    }

    if (is_function_definition_node(type) || is_function_like_declaration_node(type)) {
      if (type == "field_declaration") {
        TSNode declarator = child_by_field(node, "declarator");
        if (ts_node_is_null(declarator)) {
          walk_named_children(node, std::move(ctx), symbols);
          return;
        }

        const std::string_view declarator_type{ts_node_type(declarator)};
        if (declarator_type != "function_declarator" &&
            declarator_type != "function_field_declarator" && declarator_type != "operator_cast" &&
            declarator_type != "reference_declarator") {
          walk_named_children(node, std::move(ctx), symbols);
          return;
        }
      } else if (type == "declaration") {
        TSNode declarator = child_by_field(node, "declarator");
        if (ts_node_is_null(declarator)) {
          walk_named_children(node, std::move(ctx), symbols);
          return;
        }

        const std::string_view declarator_type{ts_node_type(declarator)};
        if (declarator_type != "function_declarator" &&
            declarator_type != "function_field_declarator" && declarator_type != "operator_cast" &&
            declarator_type != "reference_declarator") {
          walk_named_children(node, std::move(ctx), symbols);
          return;
        }
      }

      emit_function_like(node, std::move(ctx), symbols);
      return;
    }

    walk_named_children(node, std::move(ctx), symbols);
  }

  std::string_view source_;
  std::unordered_set<std::string> known_classes_;
};

[[nodiscard]] std::string read_file(const std::filesystem::path& file_path) {
  std::ifstream stream{file_path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{std::string{"Failed to open C++ source file: "} + file_path.string()};
  }

  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

std::string_view CppParser::module_name() const noexcept {
  return "parser";
}

bool CppParser::ready() const noexcept {
  return tree_sitter_cpp() != nullptr;
}

CppParser::Symbols CppParser::parse_file(const std::filesystem::path& file_path) const {
  const std::string source = read_file(file_path);

  ParserHandle parser{ts_parser_new()};
  if (!parser) {
    throw std::runtime_error{"Failed to create tree-sitter parser"};
  }

  if (!ts_parser_set_language(parser.get(), tree_sitter_cpp())) {
    throw std::runtime_error{"Failed to set tree-sitter C++ language"};
  }

  TreeHandle tree{ts_parser_parse_string_encoding(parser.get(), nullptr, source.data(),
                                                  static_cast<std::uint32_t>(source.size()),
                                                  TSInputEncodingUTF8)};
  if (!tree) {
    throw std::runtime_error{std::string{"tree-sitter parsing failed for: "} + file_path.string()};
  }

  SymbolCollector collector{source};
  return collector.collect(ts_tree_root_node(tree.get()));
}

} // namespace qodeloc::core
