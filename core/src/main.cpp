#include <algorithm>
#include <boost/asio.hpp>
#include <csignal>
#include <memory>
#include <qodeloc/core/api.hpp>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/embedder.hpp>
#include <qodeloc/core/indexer.hpp>
#include <qodeloc/core/llm.hpp>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/parser.hpp>
#include <qodeloc/core/retriever.hpp>
#include <qodeloc/core/storage.hpp>
#include <spdlog/spdlog.h>
#include <string_view>
#include <thread>
#include <vector>

namespace core = qodeloc::core;
namespace asio = boost::asio;

#ifndef QODELOC_CORE_VERSION
#define QODELOC_CORE_VERSION "unknown"
#endif

#ifndef QODELOC_CORE_BUILD_TYPE
#define QODELOC_CORE_BUILD_TYPE "unknown"
#endif

namespace {

[[nodiscard]] bool has_flag(int argc, char* argv[], std::string_view flag) {
  return std::any_of(argv + 1, argv + argc,
                     [flag](char* argument) { return std::string_view(argument) == flag; });
}

} // namespace

int main(int argc, char* argv[]) {
  try {
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] [qodeloc-core] %v");
    spdlog::set_level(spdlog::level::info);

    const auto& config = core::Config::current();
    const bool smoke_mode = has_flag(argc, argv, "--smoke");
    spdlog::info("QodeLoc Core {} booting", QODELOC_CORE_VERSION);
    spdlog::info("Project version: {}", QODELOC_CORE_PROJECT_VERSION);
    spdlog::info("Build type: {}", QODELOC_CORE_BUILD_TYPE);
    if (!config.env_file_path().empty()) {
      spdlog::info("Config file: {}", config.env_file_path().generic_string());
    } else {
      spdlog::warn("No .env file found; using built-in defaults");
    }
    spdlog::info("Config root: {}", config.root_directory().generic_string());
    spdlog::info("Prompts dir: {}",
                 config.prompt_builder_options().templates_directory.generic_string());
    spdlog::info("LLM endpoint: {}:{}", config.llm_options().host, config.llm_options().port);
    spdlog::info("Embedder endpoint: {}:{}", config.embedder_options().host,
                 config.embedder_options().port);
    spdlog::info("Qdrant endpoint: {}:{} collection={} enabled={}",
                 config.vector_store_options().host, config.vector_store_options().port,
                 config.vector_store_options().collection,
                 config.vector_store_options().enabled ? "true" : "false");
    spdlog::info("API endpoint: {}:{}", config.api_options().host, config.api_options().port);

    if (smoke_mode) {
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

    const auto index_root = config.indexer_root_directory().empty()
                                ? config.root_directory()
                                : config.indexer_root_directory();
    core::Indexer indexer{config.indexer_options(index_root), config.storage_database_path()};
    core::Retriever retriever{config.retriever_options()};
    core::PromptBuilder prompt_builder{config.prompt_builder_options()};
    core::LlmClient llm_client{config.llm_options()};
    core::ApiServer api_server{config.api_options()};

    api_server.attach_indexer(indexer);
    api_server.attach_retriever(retriever);
    api_server.attach_prompt_builder(prompt_builder);
    api_server.attach_llm_client(llm_client);

    api_server.set_bootstrap_state("initializing", "Initializing API and preparing initial corpus");
    api_server.start();
    spdlog::info("API server listening on {}:{}", api_server.status().host,
                 api_server.bound_port());

    std::thread bootstrap_thread([&] {
      try {
        api_server.set_bootstrap_state("indexing", "Indexing initial corpus");
        if (!indexer.ready()) {
          const auto message = "Indexer root is not configured; API is available but corpus is not";
          spdlog::warn("{}", message);
          api_server.set_bootstrap_state("error", message);
          return;
        }

        spdlog::info("Bootstrapping initial index from {}",
                     indexer.options().root_directory.generic_string());
        const auto result = indexer.index();
        try {
          retriever.build(indexer.symbols());
        } catch (const std::exception& error) {
          const auto message = std::string("Retriever bootstrap failed: ") + error.what();
          spdlog::warn("{}", message);
          api_server.set_bootstrap_state("error", message);
          return;
        }

        const auto ready_message =
            "Initial corpus ready: " + std::to_string(result.stats.files_indexed) + " files, " +
            std::to_string(result.stats.symbols_indexed) + " symbols";
        api_server.set_bootstrap_state("ready", ready_message);
        spdlog::info("{}", ready_message);
      } catch (const std::exception& error) {
        const auto message = std::string("Initial index failed: ") + error.what();
        api_server.set_bootstrap_state("error", message);
        spdlog::warn("{}", message);
      }
    });

    asio::io_context signal_io;
    asio::signal_set signals(signal_io, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
      spdlog::info("Shutdown signal received");
      api_server.stop();
      signal_io.stop();
    });

    signal_io.run();
    api_server.stop();
    if (bootstrap_thread.joinable()) {
      bootstrap_thread.join();
    }
    spdlog::info("QodeLoc Core stopped");
    return 0;
  } catch (const std::exception& error) {
    spdlog::error("QodeLoc Core failed: {}", error.what());
  } catch (...) {
    spdlog::error("QodeLoc Core failed with an unknown exception");
  }

  return 1;
}
