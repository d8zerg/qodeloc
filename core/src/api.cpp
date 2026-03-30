#include <qodeloc/core/api.hpp>

namespace qodeloc::core {

std::string_view ApiServer::module_name() const noexcept {
  return "api";
}

bool ApiServer::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
