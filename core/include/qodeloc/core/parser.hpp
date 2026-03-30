#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <qodeloc/core/module.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qodeloc::core {

enum class SymbolKind : std::uint8_t {
  Namespace,
  Class,
  Function,
  Method,
  Template,
  Enum,
};

[[nodiscard]] constexpr std::string_view to_string(SymbolKind kind) noexcept {
  switch (kind) {
  case SymbolKind::Namespace:
    return "namespace";
  case SymbolKind::Class:
    return "class";
  case SymbolKind::Function:
    return "function";
  case SymbolKind::Method:
    return "method";
  case SymbolKind::Template:
    return "template";
  case SymbolKind::Enum:
    return "enum";
  }

  return "unknown";
}

inline std::ostream& operator<<(std::ostream& os, SymbolKind kind) {
  return os << to_string(kind);
}

struct SymbolDependencies {
  std::vector<std::string> includes;
  std::vector<std::string> outgoing_calls;
  std::vector<std::string> base_classes;

  bool operator==(const SymbolDependencies&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const SymbolDependencies& dependencies) {
  auto print_list = [&os](std::string_view label, const std::vector<std::string>& values) {
    if (values.empty()) {
      return;
    }

    os << " " << label << "=[";
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i > 0) {
        os << ", ";
      }
      os << values[i];
    }
    os << "]";
  };

  os << "deps{";
  print_list("includes", dependencies.includes);
  print_list("calls", dependencies.outgoing_calls);
  print_list("bases", dependencies.base_classes);
  return os << "}";
}

struct Symbol {
  SymbolKind kind{};
  std::string qualified_name;
  std::string signature;
  std::uint32_t start_line{};
  std::uint32_t end_line{};
  SymbolDependencies dependencies;

  Symbol() = default;
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  Symbol(SymbolKind kind_, std::string qualified_name_, std::string signature_,
         std::uint32_t start_line_, std::uint32_t end_line_, SymbolDependencies dependencies_ = {})
      : kind(kind_), qualified_name(std::move(qualified_name_)), signature(std::move(signature_)),
        start_line(start_line_), end_line(end_line_), dependencies(std::move(dependencies_)) {}
  // NOLINTEND(bugprone-easily-swappable-parameters)

  bool operator==(const Symbol&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const Symbol& symbol) {
  os << '{' << symbol.kind << " " << symbol.qualified_name << " [" << symbol.start_line << '-'
     << symbol.end_line << ']';

  if (!symbol.signature.empty()) {
    os << " sig=\"" << symbol.signature << '"';
  }

  if (!symbol.dependencies.includes.empty() || !symbol.dependencies.outgoing_calls.empty() ||
      !symbol.dependencies.base_classes.empty()) {
    os << " " << symbol.dependencies;
  }

  return os << '}';
}

class CppParser final : public IModule {
public:
  using Symbols = std::vector<Symbol>;

  CppParser() = default;

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] Symbols parse_file(const std::filesystem::path& file_path) const;
};

} // namespace qodeloc::core
