#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/git_watcher.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace qodeloc::core {
namespace {

using ::testing::UnorderedElementsAre;

class TempWorkspace {
public:
  TempWorkspace()
      : root_(std::filesystem::temp_directory_path() /
              ("qodeloc-git-watcher-tests-" +
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

[[nodiscard]] std::string shell_quote(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('\'');
  for (const char ch : value) {
    if (ch == '\'') {
      result += "'\\''";
    } else {
      result.push_back(ch);
    }
  }
  result.push_back('\'');
  return result;
}

void run_git(const std::filesystem::path& repo_root, std::string_view args) {
  const std::string command =
      "git -C " + shell_quote(repo_root.generic_string()) + " " + std::string(args);
  const int status = std::system(command.c_str());
  if (status != 0) {
    throw std::runtime_error("Git command failed: " + command);
  }
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output.is_open()) {
    throw std::runtime_error("Failed to open file: " + path.generic_string());
  }

  output << contents;
}

} // namespace

TEST(GitWatcherTest, ReturnsFilesFromPreviousCommitDiff) {
  TempWorkspace workspace;
  const auto repo_root = workspace.root() / "repo";
  std::filesystem::create_directories(repo_root / "src");
  std::filesystem::create_directories(repo_root / "include");

  write_file(repo_root / "src/alpha.cpp", "int alpha() { return 1; }\n");
  write_file(repo_root / "include/alpha.hpp", "#pragma once\nint alpha();\n");
  write_file(repo_root / "notes.txt", "baseline\n");

  run_git(repo_root, "init -q");
  run_git(repo_root, "config user.email " + shell_quote("qodeloc@example.com"));
  run_git(repo_root, "config user.name " + shell_quote("QodeLoc Tests"));
  run_git(repo_root, "add .");
  run_git(repo_root, "commit -q -m " + shell_quote("baseline"));

  write_file(repo_root / "src/alpha.cpp", "int alpha() { return 2; }\n");
  write_file(repo_root / "include/alpha.hpp", "#pragma once\nint alpha();\nint beta();\n");
  write_file(repo_root / "notes.txt", "updated\n");

  run_git(repo_root, "add .");
  run_git(repo_root, "commit -q -m " + shell_quote("update"));

  GitWatcher watcher{Config::current().git_watcher_options(repo_root)};
  const auto changed_files = watcher.changed_files();

  EXPECT_THAT(changed_files, UnorderedElementsAre(std::filesystem::path("include/alpha.hpp"),
                                                  std::filesystem::path("notes.txt"),
                                                  std::filesystem::path("src/alpha.cpp")));
}

} // namespace qodeloc::core
