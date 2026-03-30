#pragma once

#include <qodeloc/core/module.hpp>

namespace qodeloc::core {

class CppParser final : public IModule {
public:
  CppParser() = default;

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
};

} // namespace qodeloc::core
