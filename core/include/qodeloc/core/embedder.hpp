#pragma once

#include <qodeloc/core/module.hpp>

namespace qodeloc::core {

class Embedder final : public IModule {
public:
  Embedder() = default;

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
};

} // namespace qodeloc::core
