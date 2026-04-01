#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <qodeloc/core/module.hpp>
#include <qodeloc/core/parser.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace qodeloc::core {

using SymbolId = std::int64_t;
using ModuleId = std::int64_t;

struct StoredSymbol {
  std::string file_path;
  std::string module_name;
  std::string module_path;
  SymbolKind kind{};
  std::string qualified_name;
  std::string signature;
  std::uint32_t start_line{};
  std::uint32_t end_line{};

  bool operator==(const StoredSymbol&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const StoredSymbol& symbol) {
  os << '{' << symbol.kind << " " << symbol.qualified_name << " [" << symbol.start_line << '-'
     << symbol.end_line << "]";

  if (!symbol.file_path.empty()) {
    os << " file=\"" << symbol.file_path << '"';
  }

  if (!symbol.module_name.empty()) {
    os << " module=\"" << symbol.module_name << '"';
  }

  if (!symbol.signature.empty()) {
    os << " sig=\"" << symbol.signature << '"';
  }

  return os << '}';
}

struct ModuleDependency {
  std::string module_name;
  std::string module_path;
  std::size_t depth{};

  bool operator==(const ModuleDependency&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const ModuleDependency& dependency) {
  os << '{' << dependency.module_name;

  if (!dependency.module_path.empty()) {
    os << " path=\"" << dependency.module_path << '"';
  }

  return os << " depth=" << dependency.depth << '}';
}

class DependencyGraph final {
public:
  DependencyGraph();
  explicit DependencyGraph(const std::filesystem::path& database_path);

  DependencyGraph(const DependencyGraph&) = delete;
  DependencyGraph& operator=(const DependencyGraph&) = delete;
  DependencyGraph(DependencyGraph&&) noexcept = default;
  DependencyGraph& operator=(DependencyGraph&&) noexcept = default;
  ~DependencyGraph();

  [[nodiscard]] bool ready() const noexcept;

  [[nodiscard]] SymbolId write_symbol(const StoredSymbol& symbol);
  void write_call(SymbolId caller_id, SymbolId callee_id);
  void write_include(SymbolId source_id, std::string_view include_path,
                     std::string_view target_module_name = {});
  void write_inheritance(SymbolId derived_id, SymbolId base_id);

  [[nodiscard]] std::vector<StoredSymbol> callers_of(SymbolId symbol_id) const;
  [[nodiscard]] std::vector<StoredSymbol> callees_from(SymbolId symbol_id) const;
  [[nodiscard]] std::vector<ModuleDependency>
  transitive_module_dependencies(std::string_view module_name, std::size_t max_depth) const;
  [[nodiscard]] std::size_t symbol_count() const;
  [[nodiscard]] std::vector<StoredSymbol> symbols_for_file(std::string_view file_path) const;
  [[nodiscard]] std::optional<SymbolId> symbol_id_for_name(std::string_view target_name) const;

  void delete_file(std::string_view file_path);

private:
  [[nodiscard]] ModuleId ensure_module(std::string_view module_name,
                                       std::string_view module_path = {});
  [[nodiscard]] std::int64_t next_symbol_id() noexcept;
  [[nodiscard]] std::int64_t next_module_id() noexcept;
  [[nodiscard]] std::int64_t next_call_id() noexcept;
  [[nodiscard]] std::int64_t next_include_id() noexcept;
  [[nodiscard]] std::int64_t next_inheritance_id() noexcept;

  [[nodiscard]] StoredSymbol fetch_symbol(SymbolId symbol_id) const;
  [[nodiscard]] ModuleId fetch_symbol_module_id(SymbolId symbol_id) const;
  [[nodiscard]] std::int64_t fetch_scalar_int(std::string_view sql) const;
  [[nodiscard]] std::vector<StoredSymbol> fetch_symbols(std::string_view sql) const;
  [[nodiscard]] std::vector<ModuleDependency> fetch_module_dependencies(std::string_view sql) const;

  void execute(std::string_view sql) const;
  void initialize_schema();
  void refresh_counters();
  void ensure_ready() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Storage final : public IModule {
public:
  Storage();
  explicit Storage(const std::filesystem::path& database_path);

  [[nodiscard]] std::string_view module_name() const noexcept override;
  [[nodiscard]] bool ready() const noexcept override;

  [[nodiscard]] DependencyGraph& graph() noexcept;
  [[nodiscard]] const DependencyGraph& graph() const noexcept;

private:
  DependencyGraph graph_;
};

} // namespace qodeloc::core
