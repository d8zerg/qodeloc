#include <memory>
#include <qodeloc/core/api.hpp>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/parser.hpp>
#include <qodeloc/core/retriever.hpp>
#include <qodeloc/core/storage.hpp>
#include <spdlog/spdlog.h>
#include <vector>

namespace core = qodeloc::core;

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
  spdlog::info("Project version: {}", QODELOC_CORE_PROJECT_VERSION);
  spdlog::info("Build type: {}", QODELOC_CORE_BUILD_TYPE);
  spdlog::info("Registering core modules");

  std::vector<std::unique_ptr<core::IModule>> modules;
  modules.reserve(7);
  modules.emplace_back(std::make_unique<core::CppParser>());
  modules.emplace_back(std::make_unique<core::Indexer>());
  modules.emplace_back(std::make_unique<core::Retriever>());
  modules.emplace_back(std::make_unique<core::Embedder>());
  modules.emplace_back(std::make_unique<core::LlmClient>());
  modules.emplace_back(std::make_unique<core::Storage>());
  modules.emplace_back(std::make_unique<core::ApiServer>());

  for (const auto& module : modules) {
    spdlog::info("Module {} ready={}", module->module_name(), module->ready());
  }

  spdlog::info("Conan 2 and CMake presets are wired");
  return 0;
}
