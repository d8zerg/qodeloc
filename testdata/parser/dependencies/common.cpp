#include "common.hpp"

namespace deps {

std::string format_label(int value) {
  return std::to_string(value);
}

std::string duplicate_label(int value) {
  return format_label(value);
}

} // namespace deps
