#include <qodeloc/core/storage.hpp>

namespace qodeloc::core {

std::string_view Storage::module_name() const noexcept {
  return "storage";
}

bool Storage::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
