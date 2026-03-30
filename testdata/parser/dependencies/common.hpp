#pragma once

#include <string>
#include <vector>

namespace deps {

std::string format_label(int value);
std::string duplicate_label(int value);

class Base {
public:
  virtual std::string kind() const = 0;
};

} // namespace deps
