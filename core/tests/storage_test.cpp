#include <chrono>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qodeloc/core/storage.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace qodeloc::core {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

[[nodiscard]] std::string module_name(std::size_t index) {
  return "module_" + std::to_string(index);
}

[[nodiscard]] std::string symbol_suffix(std::size_t index) {
  if (index < 10) {
    return "0" + std::to_string(index);
  }

  return std::to_string(index);
}

class TempWorkspace {
public:
  TempWorkspace()
      : root_(std::filesystem::temp_directory_path() /
              ("qodeloc-storage-tests-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
    std::filesystem::create_directories(root_);
  }

  TempWorkspace(const TempWorkspace&) = delete;
  TempWorkspace& operator=(const TempWorkspace&) = delete;

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

[[nodiscard]] StoredSymbol make_class_symbol(const std::filesystem::path& file_path,
                                             std::size_t index,
                                             const std::filesystem::path& module_path) {
  const auto suffix = symbol_suffix(index);
  return StoredSymbol{file_path.generic_string(),
                      module_name(index),
                      module_path.generic_string(),
                      SymbolKind::Class,
                      module_name(index) + "::Node" + suffix,
                      {},
                      2U,
                      2U};
}

[[nodiscard]] StoredSymbol make_function_symbol(const std::filesystem::path& file_path,
                                                std::size_t index,
                                                const std::filesystem::path& module_path) {
  const auto suffix = symbol_suffix(index);
  return StoredSymbol{file_path.generic_string(),
                      module_name(index),
                      module_path.generic_string(),
                      SymbolKind::Function,
                      module_name(index) + "::process" + suffix,
                      "void process" + suffix + "()",
                      3U,
                      3U};
}

void write_fixture_file(const std::filesystem::path& file_path, std::size_t index) {
  std::filesystem::create_directories(file_path.parent_path());
  std::ofstream output(file_path);
  const auto suffix = symbol_suffix(index);
  output << "namespace " << module_name(index) << " {\n"
         << "class Node" << suffix << " {};\n"
         << "void process" << suffix << "() {}\n"
         << "} // namespace " << module_name(index) << "\n";
}

} // namespace

TEST(StorageTest, ModuleReportsReady) {
  Storage storage;

  EXPECT_TRUE(storage.ready());
  EXPECT_EQ(storage.module_name(), "storage");
  EXPECT_TRUE(storage.graph().ready());
}

TEST(StorageTest, DependencyGraphSupportsSymbolQueriesAndDeletion) {
  TempWorkspace workspace;
  DependencyGraph graph;

  ASSERT_TRUE(graph.ready());

  constexpr std::size_t kFileCount = 30;
  std::vector<SymbolId> class_ids;
  std::vector<SymbolId> function_ids;
  class_ids.reserve(kFileCount);
  function_ids.reserve(kFileCount);

  for (std::size_t index = 0; index < kFileCount; ++index) {
    const auto module_dir = workspace.root() / module_name(index);
    const auto file_path = module_dir / ("file_" + symbol_suffix(index) + ".cpp");
    write_fixture_file(file_path, index);

    const auto class_symbol = make_class_symbol(file_path, index, module_dir);
    const auto function_symbol = make_function_symbol(file_path, index, module_dir);

    class_ids.push_back(graph.write_symbol(class_symbol));
    function_ids.push_back(graph.write_symbol(function_symbol));
  }

  for (std::size_t index = 0; index + 1 < kFileCount; ++index) {
    graph.write_call(function_ids[index], function_ids[index + 1]);
    graph.write_include(function_ids[index], "file_" + symbol_suffix(index + 1) + ".hpp",
                        module_name(index + 1));
    graph.write_inheritance(class_ids[index], class_ids[index + 1]);
  }

  const auto callers_before = graph.callers_of(function_ids[15]);
  EXPECT_THAT(
      callers_before,
      ElementsAre(StoredSymbol{
          (workspace.root() / module_name(14) / "file_14.cpp").generic_string(), module_name(14),
          (workspace.root() / module_name(14)).generic_string(), SymbolKind::Function,
          module_name(14) + "::process14", "void process14()", 3U, 3U}));

  const auto callees_before = graph.callees_from(function_ids[14]);
  EXPECT_THAT(
      callees_before,
      ElementsAre(StoredSymbol{
          (workspace.root() / module_name(15) / "file_15.cpp").generic_string(), module_name(15),
          (workspace.root() / module_name(15)).generic_string(), SymbolKind::Function,
          module_name(15) + "::process15", "void process15()", 3U, 3U}));

  const auto dependencies_before = graph.transitive_module_dependencies(module_name(0), 29);
  ASSERT_EQ(dependencies_before.size(), 29U);
  EXPECT_EQ(dependencies_before.front().module_name, module_name(1));
  EXPECT_EQ(dependencies_before.front().depth, 1U);
  EXPECT_EQ(dependencies_before[14].module_name, module_name(15));
  EXPECT_EQ(dependencies_before[14].depth, 15U);
  EXPECT_EQ(dependencies_before.back().module_name, module_name(29));
  EXPECT_EQ(dependencies_before.back().depth, 29U);

  graph.delete_file((workspace.root() / module_name(15) / "file_15.cpp").generic_string());

  EXPECT_THAT(graph.callers_of(function_ids[16]), IsEmpty());
  EXPECT_THAT(graph.callees_from(function_ids[14]), IsEmpty());

  const auto dependencies_after = graph.transitive_module_dependencies(module_name(0), 29);
  ASSERT_EQ(dependencies_after.size(), 15U);
  EXPECT_EQ(dependencies_after.front().module_name, module_name(1));
  EXPECT_EQ(dependencies_after.back().module_name, module_name(15));
  EXPECT_EQ(dependencies_after.back().depth, 15U);
  EXPECT_THAT(
      dependencies_after,
      ElementsAre(ModuleDependency{module_name(1),
                                   (workspace.root() / module_name(1)).generic_string(), 1U},
                  ModuleDependency{module_name(2),
                                   (workspace.root() / module_name(2)).generic_string(), 2U},
                  ModuleDependency{module_name(3),
                                   (workspace.root() / module_name(3)).generic_string(), 3U},
                  ModuleDependency{module_name(4),
                                   (workspace.root() / module_name(4)).generic_string(), 4U},
                  ModuleDependency{module_name(5),
                                   (workspace.root() / module_name(5)).generic_string(), 5U},
                  ModuleDependency{module_name(6),
                                   (workspace.root() / module_name(6)).generic_string(), 6U},
                  ModuleDependency{module_name(7),
                                   (workspace.root() / module_name(7)).generic_string(), 7U},
                  ModuleDependency{module_name(8),
                                   (workspace.root() / module_name(8)).generic_string(), 8U},
                  ModuleDependency{module_name(9),
                                   (workspace.root() / module_name(9)).generic_string(), 9U},
                  ModuleDependency{module_name(10),
                                   (workspace.root() / module_name(10)).generic_string(), 10U},
                  ModuleDependency{module_name(11),
                                   (workspace.root() / module_name(11)).generic_string(), 11U},
                  ModuleDependency{module_name(12),
                                   (workspace.root() / module_name(12)).generic_string(), 12U},
                  ModuleDependency{module_name(13),
                                   (workspace.root() / module_name(13)).generic_string(), 13U},
                  ModuleDependency{module_name(14),
                                   (workspace.root() / module_name(14)).generic_string(), 14U},
                  ModuleDependency{module_name(15),
                                   (workspace.root() / module_name(15)).generic_string(), 15U}));
}

} // namespace qodeloc::core
