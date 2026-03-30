#pragma once

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <qodeloc/core/module.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

enum class SymbolKind {
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

struct Symbol {
  SymbolKind kind{};
  std::string qualified_name;
  std::string signature;
  std::uint32_t start_line{};
  std::uint32_t end_line{};

  bool operator==(const Symbol&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const Symbol& symbol) {
  os << '{' << symbol.kind << " " << symbol.qualified_name << " [" << symbol.start_line << '-'
     << symbol.end_line << ']';

  if (!symbol.signature.empty()) {
    os << " sig=\"" << symbol.signature << '"';
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
