#pragma once

#include <string_view>

namespace qodeloc::core {

class IModule {
public:
  virtual ~IModule() = default;

  [[nodiscard]] virtual std::string_view module_name() const noexcept = 0;
  [[nodiscard]] virtual bool ready() const noexcept = 0;
};

} // namespace qodeloc::core
