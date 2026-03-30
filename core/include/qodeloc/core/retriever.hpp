#pragma once

#include <qodeloc/core/module.hpp>

namespace qodeloc::core {

class Retriever final : public IModule {
public:
  Retriever() = default;

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;
};

} // namespace qodeloc::core
