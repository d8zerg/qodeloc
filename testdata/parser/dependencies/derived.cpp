#include "common.hpp"

#include <utility>

namespace deps {

class Derived : public Base {
public:
  std::string kind() const override {
    return duplicate_label(7);
  }

  std::string decorate() const {
    return format_label(std::move(payload_));
  }

private:
  std::string payload_{"payload"};
};

} // namespace deps
