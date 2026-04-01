#include <algorithm>
#include <array>
#include <duckdb.h>
#include <filesystem>
#include <functional>
#include <qodeloc/core/config.hpp>
#include <qodeloc/core/storage.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qodeloc::core {
namespace {

void trim_in_place(std::string& value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    value.clear();
    return;
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  value = value.substr(first, last - first + 1);
}

[[nodiscard]] std::string trimmed(std::string_view value) {
  std::string result(value);
  trim_in_place(result);
  return result;
}

[[nodiscard]] std::string normalize_path(const std::filesystem::path& path) {
  if (path.empty()) {
    return {};
  }

  return path.lexically_normal().generic_string();
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] std::string normalize_module_name(std::string_view value,
                                                std::string_view fallback = {}) {
  std::string result = trimmed(value);
  if (result.empty()) {
    result = std::string(fallback);
  }
  if (result.empty()) {
    result = "root";
  }
  return result;
}

[[nodiscard]] std::string normalize_module_path(std::string_view value,
                                                std::string_view fallback = {}) {
  std::string result = trimmed(value);
  if (result.empty()) {
    result = std::string(fallback);
  }
  if (result.empty()) {
    result = "root";
  }
  return result;
}

[[nodiscard]] std::string normalize_include_path(std::string_view value) {
  std::string result = trimmed(value);
  if (result.size() >= 2) {
    const char first = result.front();
    const char last = result.back();
    if ((first == '"' && last == '"') || (first == '<' && last == '>')) {
      result = result.substr(1, result.size() - 2);
      trim_in_place(result);
    }
  }

  return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

[[nodiscard]] std::string sql_quote(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('\'');
  for (const char ch : value) {
    if (ch == '\'') {
      result.push_back('\'');
    }
    result.push_back(ch);
  }
  result.push_back('\'');
  return result;
}

[[nodiscard]] std::string escape_like_pattern(std::string_view value) {
  std::string result;
  result.reserve(value.size() * 2);
  for (const char ch : value) {
    if (ch == '\\' || ch == '%' || ch == '_') {
      result.push_back('\\');
    }
    result.push_back(ch);
  }
  return result;
}

[[nodiscard]] std::string sql_like_suffix_pattern(std::string_view value) {
  std::string result = "%::";
  result += escape_like_pattern(value);
  return result;
}

[[nodiscard]] std::string sql_int(std::int64_t value) {
  return std::to_string(value);
}

[[nodiscard]] std::string join_ids(std::span<const std::int64_t> ids) {
  std::string result = "(";
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (index > 0) {
      result += ", ";
    }
    result += sql_int(ids[index]);
  }
  result += ")";
  return result;
}

[[nodiscard]] std::string join_quoted_strings(const std::vector<std::string>& values) {
  std::string result = "(";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      result += ", ";
    }
    result += sql_quote(values[index]);
  }
  result += ")";
  return result;
}

class ResultGuard {
public:
  ResultGuard() = default;
  ResultGuard(const ResultGuard&) = delete;
  ResultGuard& operator=(const ResultGuard&) = delete;

  ~ResultGuard() {
    duckdb_destroy_result(&result);
  }

  duckdb_result result{};
};

template <typename Fn>
decltype(auto) with_query(duckdb_connection connection, std::string_view sql, Fn&& fn) {
  ResultGuard guard;
  const auto status = duckdb_query(connection, std::string(sql).c_str(), &guard.result);
  if (status != DuckDBSuccess) {
    const char* error = duckdb_result_error(&guard.result);
    const std::string message = error != nullptr ? error : "unknown error";
    throw std::runtime_error("DuckDB query failed: " + message);
  }

  if constexpr (std::is_void_v<std::invoke_result_t<Fn, duckdb_result&>>) {
    std::invoke(std::forward<Fn>(fn), guard.result);
  } else {
    return std::invoke(std::forward<Fn>(fn), guard.result);
  }
}

[[nodiscard]] std::string value_string(duckdb_result* result, idx_t column, idx_t row) {
  char* raw = duckdb_value_varchar(result, column, row);
  if (raw == nullptr) {
    return {};
  }

  std::string value(raw);
  duckdb_free(raw);
  return value;
}

[[nodiscard]] StoredSymbol read_symbol_row(duckdb_result* result, idx_t row) {
  StoredSymbol symbol;
  symbol.file_path = value_string(result, 0, row);
  symbol.module_name = value_string(result, 1, row);
  symbol.module_path = value_string(result, 2, row);
  symbol.kind = static_cast<SymbolKind>(duckdb_value_uint8(result, 3, row));
  symbol.qualified_name = value_string(result, 4, row);
  symbol.signature = value_string(result, 5, row);
  symbol.start_line = static_cast<std::uint32_t>(duckdb_value_int64(result, 6, row));
  symbol.end_line = static_cast<std::uint32_t>(duckdb_value_int64(result, 7, row));
  return symbol;
}

[[nodiscard]] ModuleDependency read_module_dependency_row(duckdb_result* result, idx_t row) {
  ModuleDependency dependency;
  dependency.module_name = value_string(result, 0, row);
  dependency.module_path = value_string(result, 1, row);
  dependency.depth = static_cast<std::size_t>(duckdb_value_int64(result, 2, row));
  return dependency;
}

[[nodiscard]] std::string symbol_projection_sql() {
  return "s.file_path, m.module_name, m.module_path, s.kind, s.qualified_name, s.signature, "
         "s.start_line, s.end_line";
}

class DuckDbStorageStore {
public:
  explicit DuckDbStorageStore(const std::filesystem::path& database_path) {
    try {
      std::string path_string;
      const char* path_ptr = nullptr;
      if (!database_path.empty() && database_path != ":memory:") {
        path_string = database_path.string();
        path_ptr = path_string.c_str();
      }

      if (duckdb_open(path_ptr, &database_) != DuckDBSuccess) {
        cleanup();
        return;
      }

      if (duckdb_connect(database_, &connection_) != DuckDBSuccess) {
        cleanup();
        return;
      }

      ready_ = true;
      initialize_schema();
      refresh_counters();
    } catch (...) {
      cleanup();
      ready_ = false;
    }
  }

  DuckDbStorageStore(const DuckDbStorageStore&) = delete;
  DuckDbStorageStore& operator=(const DuckDbStorageStore&) = delete;
  DuckDbStorageStore(DuckDbStorageStore&&) = delete;
  DuckDbStorageStore& operator=(DuckDbStorageStore&&) = delete;

  ~DuckDbStorageStore() {
    cleanup();
  }

  [[nodiscard]] bool ready() const noexcept {
    return ready_;
  }

  void begin_transaction() {
    ensure_ready();
    if (transaction_open_) {
      throw std::runtime_error("DuckDB transaction is already open");
    }
    execute("BEGIN TRANSACTION");
    transaction_open_ = true;
  }

  void commit_transaction() {
    ensure_ready();
    if (!transaction_open_) {
      return;
    }
    execute("COMMIT");
    transaction_open_ = false;
  }

  void rollback_transaction() noexcept {
    if (!transaction_open_) {
      return;
    }

    try {
      execute("ROLLBACK");
    } catch (const std::exception& rollback_error) {
      (void)rollback_error;
      // Best effort rollback; preserve the original failure.
    }
    transaction_open_ = false;
  }

  [[nodiscard]] SymbolId write_symbol(const StoredSymbol& symbol) {
    const auto symbols = std::array<StoredSymbol, 1>{symbol};
    return write_symbols(symbols).front();
  }

  [[nodiscard]] std::vector<SymbolId> write_symbols(std::span<const StoredSymbol> symbols) {
    ensure_ready();
    if (symbols.empty()) {
      return {};
    }

    struct TransactionScope {
      explicit TransactionScope(DuckDbStorageStore& store_) : store(store_) {
        started = store.begin_transaction_if_needed();
      }

      ~TransactionScope() {
        if (started && !committed) {
          store.rollback_transaction();
        }
      }

      void commit() {
        if (started && !committed) {
          store.commit_transaction();
          committed = true;
        }
      }

      DuckDbStorageStore& store;
      bool started{false};
      bool committed{false};
    };

    TransactionScope transaction(*this);

    std::vector<SymbolId> symbol_ids;
    symbol_ids.reserve(symbols.size());
    std::unordered_map<std::string, ModuleId> module_cache;
    module_cache.reserve(symbols.size());

    std::string insert_sql =
        "INSERT INTO symbols (symbol_id, module_id, file_path, kind, qualified_name, signature, "
        "start_line, end_line) VALUES ";

    for (std::size_t index = 0; index < symbols.size(); ++index) {
      const auto& symbol = symbols[index];
      const std::string normalized_file_path =
          normalize_path(std::filesystem::path(symbol.file_path));
      const std::string normalized_module_name = normalize_module_name(
          symbol.module_name,
          std::filesystem::path(normalized_file_path).parent_path().generic_string());
      const std::string normalized_module_path =
          normalize_module_path(symbol.module_path, normalized_module_name);
      const std::string module_key = normalized_module_name + '\x1f' + normalized_module_path;

      ModuleId module_id{};
      if (const auto cached = module_cache.find(module_key); cached != module_cache.end()) {
        module_id = cached->second;
      } else {
        module_id = ensure_module(normalized_module_name, normalized_module_path);
        module_cache.emplace(module_key, module_id);
      }

      const auto symbol_id = next_symbol_id();
      symbol_ids.push_back(symbol_id);
      if (index > 0) {
        insert_sql += ", ";
      }
      insert_sql += "(" + sql_int(symbol_id) + ", " + sql_int(module_id) + ", " +
                    sql_quote(normalized_file_path) + ", " +
                    sql_int(static_cast<std::int64_t>(symbol.kind)) + ", " +
                    sql_quote(symbol.qualified_name) + ", " + sql_quote(symbol.signature) + ", " +
                    sql_int(static_cast<std::int64_t>(symbol.start_line)) + ", " +
                    sql_int(static_cast<std::int64_t>(symbol.end_line)) + ")";
    }

    execute(insert_sql);
    transaction.commit();
    return symbol_ids;
  }

  void write_call(SymbolId caller_id, SymbolId callee_id) {
    const auto edges = std::array<CallEdge, 1>{CallEdge{caller_id, callee_id}};
    write_calls(edges);
  }

  void write_calls(std::span<const CallEdge> calls) {
    ensure_ready();
    if (calls.empty()) {
      return;
    }

    struct TransactionScope {
      explicit TransactionScope(DuckDbStorageStore& store_) : store(store_) {
        started = store.begin_transaction_if_needed();
      }

      ~TransactionScope() {
        if (started && !committed) {
          store.rollback_transaction();
        }
      }

      void commit() {
        if (started && !committed) {
          store.commit_transaction();
          committed = true;
        }
      }

      DuckDbStorageStore& store;
      bool started{false};
      bool committed{false};
    };

    TransactionScope transaction(*this);

    std::vector<SymbolId> symbol_ids;
    symbol_ids.reserve(calls.size() * 2);
    for (const auto& edge : calls) {
      symbol_ids.push_back(edge.caller_id);
      symbol_ids.push_back(edge.callee_id);
    }
    const auto module_ids = fetch_symbol_module_map(symbol_ids);

    std::string insert_sql =
        "INSERT INTO calls (call_id, caller_symbol_id, callee_symbol_id, caller_module_id, "
        "callee_module_id) VALUES ";
    for (std::size_t index = 0; index < calls.size(); ++index) {
      const auto& edge = calls[index];
      const auto caller_module = module_ids.find(edge.caller_id);
      const auto callee_module = module_ids.find(edge.callee_id);
      if (caller_module == module_ids.end() || callee_module == module_ids.end()) {
        throw std::runtime_error("Unknown symbol id in batched call edge");
      }

      if (index > 0) {
        insert_sql += ", ";
      }
      insert_sql += "(" + sql_int(next_call_id()) + ", " + sql_int(edge.caller_id) + ", " +
                    sql_int(edge.callee_id) + ", " + sql_int(caller_module->second) + ", " +
                    sql_int(callee_module->second) + ")";
    }

    execute(insert_sql);
    transaction.commit();
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void write_include(SymbolId source_id, std::string_view include_path,
                     std::string_view target_module_name) {
    const auto includes = std::array<IncludeEdge, 1>{
        IncludeEdge{source_id, std::string(include_path), std::string(target_module_name)}};
    write_includes(includes);
  }

  void write_includes(std::span<const IncludeEdge> includes) {
    ensure_ready();
    if (includes.empty()) {
      return;
    }

    struct TransactionScope {
      explicit TransactionScope(DuckDbStorageStore& store_) : store(store_) {
        started = store.begin_transaction_if_needed();
      }

      ~TransactionScope() {
        if (started && !committed) {
          store.rollback_transaction();
        }
      }

      void commit() {
        if (started && !committed) {
          store.commit_transaction();
          committed = true;
        }
      }

      DuckDbStorageStore& store;
      bool started{false};
      bool committed{false};
    };

    TransactionScope transaction(*this);

    std::vector<SymbolId> source_ids;
    source_ids.reserve(includes.size());
    for (const auto& edge : includes) {
      source_ids.push_back(edge.source_id);
    }
    const auto module_ids = fetch_symbol_module_map(source_ids);

    std::unordered_map<std::string, ModuleId> target_module_cache;
    target_module_cache.reserve(includes.size());

    std::string insert_sql =
        "INSERT INTO includes (include_id, source_symbol_id, include_path, source_module_id, "
        "target_module_id) VALUES ";
    for (std::size_t index = 0; index < includes.size(); ++index) {
      const auto& edge = includes[index];
      const auto source_module = module_ids.find(edge.source_id);
      if (source_module == module_ids.end()) {
        throw std::runtime_error("Unknown source symbol id in batched include edge");
      }

      ModuleId target_module_id = 0;
      if (!edge.target_module_name.empty()) {
        const auto cached = target_module_cache.find(edge.target_module_name);
        if (cached != target_module_cache.end()) {
          target_module_id = cached->second;
        } else {
          target_module_id = ensure_module(edge.target_module_name);
          target_module_cache.emplace(edge.target_module_name, target_module_id);
        }
      }

      if (index > 0) {
        insert_sql += ", ";
      }
      insert_sql += "(" + sql_int(next_include_id()) + ", " + sql_int(edge.source_id) + ", " +
                    sql_quote(normalize_include_path(edge.include_path)) + ", " +
                    sql_int(source_module->second) + ", ";
      if (target_module_id > 0) {
        insert_sql += sql_int(target_module_id);
      } else {
        insert_sql += "NULL";
      }
      insert_sql += ")";
    }

    execute(insert_sql);
    transaction.commit();
  }

  void write_inheritance(SymbolId derived_id, SymbolId base_id) {
    const auto edges = std::array<InheritanceEdge, 1>{InheritanceEdge{derived_id, base_id}};
    write_inheritances(edges);
  }

  void write_inheritances(std::span<const InheritanceEdge> inheritances) {
    ensure_ready();
    if (inheritances.empty()) {
      return;
    }

    struct TransactionScope {
      explicit TransactionScope(DuckDbStorageStore& store_) : store(store_) {
        started = store.begin_transaction_if_needed();
      }

      ~TransactionScope() {
        if (started && !committed) {
          store.rollback_transaction();
        }
      }

      void commit() {
        if (started && !committed) {
          store.commit_transaction();
          committed = true;
        }
      }

      DuckDbStorageStore& store;
      bool started{false};
      bool committed{false};
    };

    TransactionScope transaction(*this);

    std::vector<SymbolId> symbol_ids;
    symbol_ids.reserve(inheritances.size() * 2);
    for (const auto& edge : inheritances) {
      symbol_ids.push_back(edge.derived_id);
      symbol_ids.push_back(edge.base_id);
    }
    const auto module_ids = fetch_symbol_module_map(symbol_ids);

    std::string insert_sql =
        "INSERT INTO inheritance (inheritance_id, derived_symbol_id, base_symbol_id, "
        "derived_module_id, base_module_id) VALUES ";
    for (std::size_t index = 0; index < inheritances.size(); ++index) {
      const auto& edge = inheritances[index];
      const auto derived_module = module_ids.find(edge.derived_id);
      const auto base_module = module_ids.find(edge.base_id);
      if (derived_module == module_ids.end() || base_module == module_ids.end()) {
        throw std::runtime_error("Unknown symbol id in batched inheritance edge");
      }

      if (index > 0) {
        insert_sql += ", ";
      }
      insert_sql += "(" + sql_int(next_inheritance_id()) + ", " + sql_int(edge.derived_id) + ", " +
                    sql_int(edge.base_id) + ", " + sql_int(derived_module->second) + ", " +
                    sql_int(base_module->second) + ")";
    }

    execute(insert_sql);
    transaction.commit();
  }

  [[nodiscard]] std::vector<StoredSymbol> callers_of(SymbolId symbol_id) const {
    return fetch_symbols(
        "SELECT DISTINCT " + symbol_projection_sql() +
        " FROM calls c JOIN symbols s ON s.symbol_id = c.caller_symbol_id JOIN modules m ON "
        "m.module_id = s.module_id WHERE c.callee_symbol_id = " +
        sql_int(symbol_id) + " ORDER BY s.file_path, s.qualified_name, s.start_line, s.end_line");
  }

  [[nodiscard]] std::vector<StoredSymbol> callees_from(SymbolId symbol_id) const {
    return fetch_symbols(
        "SELECT DISTINCT " + symbol_projection_sql() +
        " FROM calls c JOIN symbols s ON s.symbol_id = c.callee_symbol_id JOIN modules m ON "
        "m.module_id = s.module_id WHERE c.caller_symbol_id = " +
        sql_int(symbol_id) + " ORDER BY s.file_path, s.qualified_name, s.start_line, s.end_line");
  }

  [[nodiscard]] std::vector<ModuleDependency>
  transitive_module_dependencies(std::string_view module_name, std::size_t max_depth) const {
    if (max_depth == 0) {
      return {};
    }

    ensure_ready();

    const std::string normalized_module_name = normalize_module_name(module_name);
    const auto source_rows = fetch_module_dependencies(
        "SELECT module_name, module_path, 0 AS depth FROM modules WHERE module_name = " +
        sql_quote(normalized_module_name) + " LIMIT 1");
    if (source_rows.empty()) {
      return {};
    }

    const auto source_module_sql =
        "SELECT module_id FROM modules WHERE module_name = " + sql_quote(normalized_module_name) +
        " LIMIT 1";
    const auto source_module_id = fetch_scalar_int(source_module_sql);
    if (source_module_id <= 0) {
      return {};
    }

    std::vector<ModuleDependency> dependencies;
    std::vector<ModuleId> frontier{source_module_id};
    std::unordered_set<ModuleId> visited{source_module_id};
    dependencies.reserve(max_depth);

    for (std::size_t depth = 1; depth <= max_depth && !frontier.empty(); ++depth) {
      std::vector<ModuleId> next_ids;

      const auto frontier_clause = join_ids(frontier);
      const auto call_ids = fetch_module_ids(
          "SELECT DISTINCT callee_module_id FROM calls WHERE caller_module_id IN " +
          frontier_clause);
      const auto inheritance_ids = fetch_module_ids(
          "SELECT DISTINCT base_module_id FROM inheritance WHERE derived_module_id IN " +
          frontier_clause);
      const auto include_ids = fetch_module_ids(
          "SELECT DISTINCT target_module_id FROM includes WHERE target_module_id IS NOT NULL AND "
          "source_module_id IN " +
          frontier_clause);

      next_ids.reserve(call_ids.size() + inheritance_ids.size() + include_ids.size());
      next_ids.insert(next_ids.end(), call_ids.begin(), call_ids.end());
      next_ids.insert(next_ids.end(), inheritance_ids.begin(), inheritance_ids.end());
      next_ids.insert(next_ids.end(), include_ids.begin(), include_ids.end());

      std::sort(next_ids.begin(), next_ids.end());
      next_ids.erase(std::unique(next_ids.begin(), next_ids.end()), next_ids.end());

      std::vector<ModuleId> next_frontier;
      for (const auto module_id : next_ids) {
        if (visited.insert(module_id).second) {
          next_frontier.push_back(module_id);
          const auto rows = fetch_module_dependencies(
              "SELECT module_name, module_path, " + sql_int(static_cast<std::int64_t>(depth)) +
              " AS depth FROM modules WHERE module_id = " + sql_int(module_id) + " LIMIT 1");
          if (!rows.empty()) {
            dependencies.push_back(rows.front());
          }
        }
      }

      frontier = std::move(next_frontier);
    }

    return dependencies;
  }

  [[nodiscard]] std::size_t symbol_count() const {
    ensure_ready();
    return static_cast<std::size_t>(fetch_scalar_int("SELECT COUNT(*) FROM symbols"));
  }

  [[nodiscard]] std::vector<StoredSymbol> symbols_for_file(std::string_view file_path) const {
    ensure_ready();
    const std::string normalized_file_path = normalize_path(std::filesystem::path(file_path));
    return fetch_symbols("SELECT " + symbol_projection_sql() +
                         " FROM symbols s JOIN modules m ON m.module_id = s.module_id WHERE "
                         "s.file_path = " +
                         sql_quote(normalized_file_path) +
                         " ORDER BY s.file_path, s.qualified_name, s.start_line, s.end_line");
  }

  [[nodiscard]] std::optional<SymbolId> symbol_id_for_name(std::string_view target_name) const {
    ensure_ready();

    const auto exact_match = fetch_scalar_int(
        "SELECT symbol_id FROM symbols WHERE qualified_name = " + sql_quote(target_name) +
        " LIMIT 1");
    if (exact_match > 0) {
      return exact_match;
    }

    if (target_name.find("::") == std::string_view::npos) {
      const auto suffix_match =
          fetch_scalar_int("SELECT symbol_id FROM symbols WHERE qualified_name LIKE " +
                           sql_quote(sql_like_suffix_pattern(target_name)) +
                           " ESCAPE '\\' "
                           "ORDER BY LENGTH(qualified_name), "
                           "qualified_name LIMIT 1");
      if (suffix_match > 0) {
        return suffix_match;
      }
    }

    return std::nullopt;
  }

  void delete_file(std::string_view file_path) {
    const auto files = std::array<std::filesystem::path, 1>{std::filesystem::path(file_path)};
    delete_files(files);
  }

  void delete_files(std::span<const std::filesystem::path> file_paths) {
    ensure_ready();
    if (file_paths.empty()) {
      return;
    }

    struct TransactionScope {
      explicit TransactionScope(DuckDbStorageStore& store_) : store(store_) {
        started = store.begin_transaction_if_needed();
      }

      ~TransactionScope() {
        if (started && !committed) {
          store.rollback_transaction();
        }
      }

      void commit() {
        if (started && !committed) {
          store.commit_transaction();
          committed = true;
        }
      }

      DuckDbStorageStore& store;
      bool started{false};
      bool committed{false};
    };

    TransactionScope transaction(*this);

    std::vector<std::string> normalized_file_paths;
    normalized_file_paths.reserve(file_paths.size());
    for (const auto& file_path : file_paths) {
      normalized_file_paths.push_back(normalize_path(file_path));
    }

    const std::string symbol_ids = "(SELECT symbol_id FROM symbols WHERE file_path IN " +
                                   join_quoted_strings(normalized_file_paths) + ")";

    execute("DELETE FROM calls WHERE caller_symbol_id IN " + symbol_ids +
            " OR callee_symbol_id IN " + symbol_ids);
    execute("DELETE FROM includes WHERE source_symbol_id IN " + symbol_ids);
    execute("DELETE FROM inheritance WHERE derived_symbol_id IN " + symbol_ids +
            " OR base_symbol_id IN " + symbol_ids);
    execute("DELETE FROM symbols WHERE file_path IN " + join_quoted_strings(normalized_file_paths));

    transaction.commit();
  }

private:
  [[nodiscard]] ModuleId ensure_module(std::string_view module_name,
                                       std::string_view module_path = {}) {
    ensure_ready();

    const std::string normalized_name = normalize_module_name(module_name, module_path);
    const std::string normalized_path = normalize_module_path(module_path, normalized_name);

    const std::string lookup_sql =
        "SELECT module_id FROM modules WHERE module_name = " + sql_quote(normalized_name) +
        " LIMIT 1";
    const auto existing_id = fetch_scalar_int(lookup_sql);
    if (existing_id > 0) {
      return existing_id;
    }

    const auto module_id = next_module_id();
    const std::string insert_sql =
        "INSERT INTO modules (module_id, module_name, module_path) VALUES (" + sql_int(module_id) +
        ", " + sql_quote(normalized_name) + ", " + sql_quote(normalized_path) + ")";
    execute(insert_sql);
    return module_id;
  }

  [[nodiscard]] std::int64_t next_symbol_id() noexcept {
    return next_symbol_id_++;
  }

  [[nodiscard]] std::int64_t next_module_id() noexcept {
    return next_module_id_++;
  }

  [[nodiscard]] std::int64_t next_call_id() noexcept {
    return next_call_id_++;
  }

  [[nodiscard]] std::int64_t next_include_id() noexcept {
    return next_include_id_++;
  }

  [[nodiscard]] std::int64_t next_inheritance_id() noexcept {
    return next_inheritance_id_++;
  }

  [[nodiscard]] StoredSymbol fetch_symbol(SymbolId symbol_id) const {
    const auto rows = fetch_symbols(
        "SELECT " + symbol_projection_sql() +
        " FROM symbols s JOIN modules m ON m.module_id = s.module_id WHERE s.symbol_id = " +
        sql_int(symbol_id) + " LIMIT 1");
    if (rows.empty()) {
      throw std::runtime_error("Unknown symbol id: " + sql_int(symbol_id));
    }
    return rows.front();
  }

  [[nodiscard]] ModuleId fetch_symbol_module_id(SymbolId symbol_id) const {
    return fetch_scalar_int(
        "SELECT module_id FROM symbols WHERE symbol_id = " + sql_int(symbol_id) + " LIMIT 1");
  }

  [[nodiscard]] std::unordered_map<SymbolId, ModuleId>
  fetch_symbol_module_map(std::span<const SymbolId> symbol_ids) const {
    ensure_ready();
    std::unordered_map<SymbolId, ModuleId> module_ids;
    if (symbol_ids.empty()) {
      return module_ids;
    }

    std::vector<SymbolId> unique_ids(symbol_ids.begin(), symbol_ids.end());
    std::sort(unique_ids.begin(), unique_ids.end());
    unique_ids.erase(std::unique(unique_ids.begin(), unique_ids.end()), unique_ids.end());

    const auto sql =
        "SELECT symbol_id, module_id FROM symbols WHERE symbol_id IN " + join_ids(unique_ids);
    return with_query(connection_, sql, [&module_ids](duckdb_result& result) {
      module_ids.reserve(duckdb_row_count(&result));
      for (idx_t row = 0; row < duckdb_row_count(&result); ++row) {
        const auto symbol_id = static_cast<SymbolId>(duckdb_value_int64(&result, 0, row));
        const auto module_id = static_cast<ModuleId>(duckdb_value_int64(&result, 1, row));
        module_ids.emplace(symbol_id, module_id);
      }
      return module_ids;
    });
  }

  [[nodiscard]] std::int64_t fetch_scalar_int(std::string_view sql) const {
    ensure_ready();
    return with_query(connection_, sql, [](duckdb_result& result) {
      if (duckdb_row_count(&result) == 0) {
        return std::int64_t{0};
      }
      return static_cast<std::int64_t>(duckdb_value_int64(&result, 0, 0));
    });
  }

  [[nodiscard]] std::vector<StoredSymbol> fetch_symbols(std::string_view sql) const {
    ensure_ready();
    return with_query(connection_, sql, [](duckdb_result& result) {
      std::vector<StoredSymbol> rows;
      rows.reserve(duckdb_row_count(&result));
      for (idx_t row = 0; row < duckdb_row_count(&result); ++row) {
        rows.push_back(read_symbol_row(&result, row));
      }
      return rows;
    });
  }

  [[nodiscard]] std::vector<ModuleDependency>
  fetch_module_dependencies(std::string_view sql) const {
    ensure_ready();
    return with_query(connection_, sql, [](duckdb_result& result) {
      std::vector<ModuleDependency> rows;
      rows.reserve(duckdb_row_count(&result));
      for (idx_t row = 0; row < duckdb_row_count(&result); ++row) {
        rows.push_back(read_module_dependency_row(&result, row));
      }
      return rows;
    });
  }

  [[nodiscard]] std::vector<ModuleId> fetch_module_ids(std::string_view sql) const {
    ensure_ready();
    return with_query(connection_, sql, [](duckdb_result& result) {
      std::vector<ModuleId> ids;
      ids.reserve(duckdb_row_count(&result));
      for (idx_t row = 0; row < duckdb_row_count(&result); ++row) {
        ids.push_back(static_cast<ModuleId>(duckdb_value_int64(&result, 0, row)));
      }

      std::sort(ids.begin(), ids.end());
      ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
      return ids;
    });
  }

  void execute(std::string_view sql) const {
    ensure_ready();
    with_query(connection_, sql, [](duckdb_result&) {});
  }

  [[nodiscard]] bool begin_transaction_if_needed() {
    if (transaction_open_) {
      return false;
    }
    execute("BEGIN TRANSACTION");
    transaction_open_ = true;
    return true;
  }

  void initialize_schema() {
    execute("CREATE TABLE IF NOT EXISTS modules ("
            "module_id BIGINT PRIMARY KEY, "
            "module_name VARCHAR NOT NULL, "
            "module_path VARCHAR NOT NULL)");

    execute("CREATE TABLE IF NOT EXISTS symbols ("
            "symbol_id BIGINT PRIMARY KEY, "
            "module_id BIGINT NOT NULL, "
            "file_path VARCHAR NOT NULL, "
            "kind UTINYINT NOT NULL, "
            "qualified_name VARCHAR NOT NULL, "
            "signature VARCHAR NOT NULL, "
            "start_line BIGINT NOT NULL, "
            "end_line BIGINT NOT NULL)");

    execute("CREATE TABLE IF NOT EXISTS calls ("
            "call_id BIGINT PRIMARY KEY, "
            "caller_symbol_id BIGINT NOT NULL, "
            "callee_symbol_id BIGINT NOT NULL, "
            "caller_module_id BIGINT NOT NULL, "
            "callee_module_id BIGINT NOT NULL)");

    execute("CREATE TABLE IF NOT EXISTS includes ("
            "include_id BIGINT PRIMARY KEY, "
            "source_symbol_id BIGINT NOT NULL, "
            "include_path VARCHAR NOT NULL, "
            "source_module_id BIGINT NOT NULL, "
            "target_module_id BIGINT)");

    execute("CREATE TABLE IF NOT EXISTS inheritance ("
            "inheritance_id BIGINT PRIMARY KEY, "
            "derived_symbol_id BIGINT NOT NULL, "
            "base_symbol_id BIGINT NOT NULL, "
            "derived_module_id BIGINT NOT NULL, "
            "base_module_id BIGINT NOT NULL)");
  }

  void refresh_counters() {
    next_symbol_id_ = fetch_scalar_int("SELECT COALESCE(MAX(symbol_id), 0) + 1 FROM symbols");
    next_module_id_ = fetch_scalar_int("SELECT COALESCE(MAX(module_id), 0) + 1 FROM modules");
    next_call_id_ = fetch_scalar_int("SELECT COALESCE(MAX(call_id), 0) + 1 FROM calls");
    next_include_id_ = fetch_scalar_int("SELECT COALESCE(MAX(include_id), 0) + 1 FROM includes");
    next_inheritance_id_ =
        fetch_scalar_int("SELECT COALESCE(MAX(inheritance_id), 0) + 1 FROM inheritance");
  }

  void ensure_ready() const {
    if (!ready_) {
      throw std::runtime_error("DependencyGraph is not ready");
    }
  }

  void cleanup() noexcept {
    if (connection_ != nullptr) {
      duckdb_disconnect(&connection_);
      connection_ = nullptr;
    }
    if (database_ != nullptr) {
      duckdb_close(&database_);
      database_ = nullptr;
    }
  }

private:
  duckdb_database database_{nullptr};
  duckdb_connection connection_{nullptr};
  bool ready_{false};
  bool transaction_open_{false};
  std::int64_t next_symbol_id_{1};
  std::int64_t next_module_id_{1};
  std::int64_t next_call_id_{1};
  std::int64_t next_include_id_{1};
  std::int64_t next_inheritance_id_{1};
};

} // namespace

struct DependencyGraph::Impl {
  explicit Impl(const std::filesystem::path& database_path) : store(database_path) {}

  DuckDbStorageStore store;
};

DependencyGraph::DependencyGraph() : DependencyGraph(std::filesystem::path{}) {}

DependencyGraph::DependencyGraph(const std::filesystem::path& database_path)
    : impl_(std::make_unique<Impl>(database_path)) {}

DependencyGraph::~DependencyGraph() = default;

[[nodiscard]] bool DependencyGraph::ready() const noexcept {
  return impl_ != nullptr && impl_->store.ready();
}

void DependencyGraph::ensure_ready() const {
  if (!ready()) {
    throw std::runtime_error("DependencyGraph is not ready");
  }
}

void DependencyGraph::begin_transaction() {
  ensure_ready();
  impl_->store.begin_transaction();
}

void DependencyGraph::commit_transaction() {
  ensure_ready();
  impl_->store.commit_transaction();
}

void DependencyGraph::rollback_transaction() noexcept {
  if (impl_ == nullptr) {
    return;
  }

  impl_->store.rollback_transaction();
}

[[nodiscard]] SymbolId DependencyGraph::write_symbol(const StoredSymbol& symbol) {
  ensure_ready();
  return impl_->store.write_symbol(symbol);
}

[[nodiscard]] std::vector<SymbolId>
DependencyGraph::write_symbols(std::span<const StoredSymbol> symbols) {
  ensure_ready();
  return impl_->store.write_symbols(symbols);
}

void DependencyGraph::write_call(SymbolId caller_id, SymbolId callee_id) {
  ensure_ready();
  impl_->store.write_call(caller_id, callee_id);
}

void DependencyGraph::write_calls(std::span<const CallEdge> calls) {
  ensure_ready();
  impl_->store.write_calls(calls);
}

void DependencyGraph::write_include(SymbolId source_id, std::string_view include_path,
                                    std::string_view target_module_name) {
  ensure_ready();
  impl_->store.write_include(source_id, include_path, target_module_name);
}

void DependencyGraph::write_includes(std::span<const IncludeEdge> includes) {
  ensure_ready();
  impl_->store.write_includes(includes);
}

void DependencyGraph::write_inheritance(SymbolId derived_id, SymbolId base_id) {
  ensure_ready();
  impl_->store.write_inheritance(derived_id, base_id);
}

void DependencyGraph::write_inheritances(std::span<const InheritanceEdge> inheritances) {
  ensure_ready();
  impl_->store.write_inheritances(inheritances);
}

[[nodiscard]] std::vector<StoredSymbol> DependencyGraph::callers_of(SymbolId symbol_id) const {
  ensure_ready();
  return impl_->store.callers_of(symbol_id);
}

[[nodiscard]] std::vector<StoredSymbol> DependencyGraph::callees_from(SymbolId symbol_id) const {
  ensure_ready();
  return impl_->store.callees_from(symbol_id);
}

[[nodiscard]] std::vector<ModuleDependency>
DependencyGraph::transitive_module_dependencies(std::string_view module_name,
                                                std::size_t max_depth) const {
  ensure_ready();
  return impl_->store.transitive_module_dependencies(module_name, max_depth);
}

[[nodiscard]] std::size_t DependencyGraph::symbol_count() const {
  ensure_ready();
  return impl_->store.symbol_count();
}

[[nodiscard]] std::vector<StoredSymbol>
DependencyGraph::symbols_for_file(std::string_view file_path) const {
  ensure_ready();
  return impl_->store.symbols_for_file(file_path);
}

[[nodiscard]] std::optional<SymbolId>
DependencyGraph::symbol_id_for_name(std::string_view target_name) const {
  ensure_ready();
  return impl_->store.symbol_id_for_name(target_name);
}

void DependencyGraph::delete_file(std::string_view file_path) {
  ensure_ready();
  impl_->store.delete_file(file_path);
}

void DependencyGraph::delete_files(std::span<const std::filesystem::path> file_paths) {
  ensure_ready();
  impl_->store.delete_files(file_paths);
}

Storage::Storage() : graph_(Config::current().storage_database_path()) {}

Storage::Storage(const std::filesystem::path& database_path) : graph_(database_path) {}

[[nodiscard]] std::string_view Storage::module_name() const noexcept {
  return "storage";
}

[[nodiscard]] bool Storage::ready() const noexcept {
  return graph_.ready();
}

[[nodiscard]] DependencyGraph& Storage::graph() noexcept {
  return graph_;
}

[[nodiscard]] const DependencyGraph& Storage::graph() const noexcept {
  return graph_;
}

} // namespace qodeloc::core
