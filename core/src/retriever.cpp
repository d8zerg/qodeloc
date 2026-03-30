#include <qodeloc/core/retriever.hpp>

namespace qodeloc::core {

std::string_view Retriever::module_name() const noexcept {
  return "retriever";
}

bool Retriever::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
