#include <qodeloc/core/parser.hpp>

namespace qodeloc::core {

std::string_view CppParser::module_name() const noexcept {
  return "parser";
}

bool CppParser::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
