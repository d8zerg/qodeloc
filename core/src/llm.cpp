#include <qodeloc/core/llm.hpp>

namespace qodeloc::core {

std::string_view LlmClient::module_name() const noexcept {
  return "llm";
}

bool LlmClient::ready() const noexcept {
  return false;
}

} // namespace qodeloc::core
