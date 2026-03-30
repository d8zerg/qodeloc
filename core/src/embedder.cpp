#include <qodeloc/core/embedder.hpp>

namespace qodeloc::core {

std::string_view Embedder::module_name() const noexcept {
  return "embedder";
}

bool Embedder::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
