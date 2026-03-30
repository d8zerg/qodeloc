#include <spdlog/spdlog.h>

#ifndef QODELOC_CORE_VERSION
#define QODELOC_CORE_VERSION "unknown"
#endif

#ifndef QODELOC_CORE_BUILD_TYPE
#define QODELOC_CORE_BUILD_TYPE "unknown"
#endif

int main() {
  spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [qodeloc-core] %v");
  spdlog::set_level(spdlog::level::info);

  spdlog::info("QodeLoc Core {} booting", QODELOC_CORE_VERSION);
  spdlog::info("Build type: {}", QODELOC_CORE_BUILD_TYPE);
  spdlog::info("Conan 2 and CMake presets are wired");
  return 0;
}
