# QodeLoc Internal API

Phase 1.3-1.5 keep the C++ core module boundaries from phase 1.2 and make the parser and storage contracts concrete. The classes below are now real extension points rather than pure stubs, so later steps can build on them without reshaping the core wiring.

## Common Contract

All core modules implement `qodeloc::core::IModule`:

```cpp
class IModule {
public:
  virtual ~IModule() = default;
  virtual std::string_view module_name() const noexcept = 0;
  virtual bool ready() const noexcept = 0;
};
```

The current skeleton uses `ready() == false` for the indexer, retriever, embedder, LLM, and API modules. `CppParser::ready()` is wired to the tree-sitter C++ language and returns `true` when the dependency is available. `Storage::ready()` becomes `true` when DuckDB initializes successfully.

## Parser Contract

`qodeloc::core::CppParser` parses a C++ file and returns a sequence of symbols with dependency metadata:

```cpp
enum class SymbolKind {
  Namespace,
  Class,
  Function,
  Method,
  Template,
  Enum,
};

struct Symbol {
  SymbolKind kind;
  std::string qualified_name;
  std::string signature;
  std::uint32_t start_line;
  std::uint32_t end_line;
  SymbolDependencies dependencies;
};

struct SymbolDependencies {
  std::vector<std::string> includes;
  std::vector<std::string> outgoing_calls;
  std::vector<std::string> base_classes;
};

class CppParser {
public:
  using Symbols = std::vector<Symbol>;
  Symbols parse_file(const std::filesystem::path& file_path) const;
};
```

The parser emits namespaces, classes, templates, enums, and function/method symbols with 1-based line ranges. Function and method symbols also carry a trimmed signature string. Every symbol carries the file-level `#include` list. Function-like symbols additionally collect outgoing call names from their bodies, and class symbols collect inherited base classes.

## Storage Contract

`qodeloc::core::DependencyGraph` owns the DuckDB schema for the structural graph:

```cpp
struct StoredSymbol {
  std::string file_path;
  std::string module_name;
  std::string module_path;
  SymbolKind kind;
  std::string qualified_name;
  std::string signature;
  std::uint32_t start_line;
  std::uint32_t end_line;
};

struct ModuleDependency {
  std::string module_name;
  std::string module_path;
  std::size_t depth;
};

using SymbolId = std::int64_t;

class DependencyGraph {
public:
  SymbolId write_symbol(const StoredSymbol& symbol);
  void write_call(SymbolId caller_id, SymbolId callee_id);
  void write_include(SymbolId source_id, std::string include_path, std::string target_module_name = {});
  void write_inheritance(SymbolId derived_id, SymbolId base_id);
  std::vector<StoredSymbol> callers_of(SymbolId symbol_id) const;
  std::vector<StoredSymbol> callees_from(SymbolId symbol_id) const;
  std::vector<ModuleDependency> transitive_module_dependencies(std::string_view module_name,
                                                               std::size_t max_depth) const;
  void delete_file(std::string_view file_path);
};
```

The schema uses five tables:

- `symbols` for symbol metadata and file/module membership
- `calls` for symbol-to-symbol call edges
- `includes` for header dependencies
- `inheritance` for base/derived class edges
- `modules` for module and directory bookkeeping

`Storage` is the thin `IModule` wrapper around `DependencyGraph`, so later phases can depend on the same storage layer without reworking the core bootstrap.

## Module Map

| Library | Public class | Responsibility |
|---|---|---|
| `libparser` | `qodeloc::core::CppParser` | C++ source parsing and symbol extraction |
| `libindexer` | `qodeloc::core::Indexer` | Repository traversal and index orchestration |
| `libretriever` | `qodeloc::core::Retriever` | Query-time retrieval pipeline |
| `libembedder` | `qodeloc::core::Embedder` | Text-to-vector embedding requests |
| `libllm` | `qodeloc::core::LlmClient` | LLM HTTP client and response handling |
| `libstorage` | `qodeloc::core::Storage` | DuckDB-backed dependency graph and future vector backends |
| `libapi` | `qodeloc::core::ApiServer` | HTTP API façade for the core engine |

## Build Layout

The static archives are produced with the following output names:

- `libparser.a`
- `libindexer.a`
- `libretriever.a`
- `libembedder.a`
- `libllm.a`
- `libstorage.a`
- `libapi.a`

`qodeloc-core` links all of them so the binary exercises the complete module graph even while the implementations are still no-op placeholders.
