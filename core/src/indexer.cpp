#include <qodeloc/core/indexer.hpp>

namespace qodeloc::core {

std::string_view Indexer::module_name() const noexcept {
  return "indexer";
}

bool Indexer::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
