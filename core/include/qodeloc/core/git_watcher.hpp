#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

class GitWatcher final {
public:
  struct Options {
    std::filesystem::path repository_root;
    std::string base_ref{"HEAD~1"};
  };

  explicit GitWatcher(Options options);

  [[nodiscard]] const Options& options() const noexcept;
  [[nodiscard]] std::vector<std::filesystem::path> changed_files() const;

private:
  Options options_;
};

} // namespace qodeloc::core
