#include <array>
#include <cstdio>
#include <filesystem>
#include <qodeloc/core/git_watcher.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace qodeloc::core {
namespace {

[[nodiscard]] std::string shell_quote(std::string_view value) {
#ifdef _WIN32
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const char ch : value) {
    if (ch == '"') {
      result.push_back('\\');
    }
    result.push_back(ch);
  }
  result.push_back('"');
  return result;
#else
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
#endif
}

[[nodiscard]] std::string build_git_diff_command(const std::filesystem::path& repository_root,
                                                 std::string_view base_ref) {
  std::string command = "git -C ";
  command += shell_quote(repository_root.generic_string());
  command += " diff --name-only ";
  command += shell_quote(base_ref);
  command += " --";
  return command;
}

[[nodiscard]] int close_pipe(std::FILE* pipe) {
#ifdef _WIN32
  return _pclose(pipe);
#else
  const int status = pclose(pipe);
  if (status == -1) {
    return status;
  }

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return status;
#endif
}

} // namespace

GitWatcher::GitWatcher(Options options) : options_(std::move(options)) {}

const GitWatcher::Options& GitWatcher::options() const noexcept {
  return options_;
}

std::vector<std::filesystem::path> GitWatcher::changed_files() const {
  const std::filesystem::path repository_root =
      options_.repository_root.empty() ? std::filesystem::current_path() : options_.repository_root;
  if (!std::filesystem::exists(repository_root)) {
    throw std::runtime_error("GitWatcher repository root does not exist: " +
                             repository_root.generic_string());
  }
  if (!std::filesystem::is_directory(repository_root)) {
    throw std::runtime_error("GitWatcher repository root is not a directory: " +
                             repository_root.generic_string());
  }

  const auto command = build_git_diff_command(repository_root, options_.base_ref);
#ifdef _WIN32
  std::FILE* pipe = _popen(command.c_str(), "r");
#else
  std::FILE* pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    throw std::runtime_error("Failed to run git diff command: " + command);
  }

  std::vector<std::filesystem::path> files;
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    std::string line(buffer.data());
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }
    if (!line.empty()) {
      files.emplace_back(line);
    }
  }

  const int status = close_pipe(pipe);
  if (status != 0) {
    throw std::runtime_error("git diff command failed with status " + std::to_string(status));
  }

  return files;
}

} // namespace qodeloc::core
