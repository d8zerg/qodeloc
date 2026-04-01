# QodeLoc Internal API

Phase 1.3-1.9 keep the C++ core module boundaries from phase 1.2 and make the parser, storage, embedding, and hierarchy contracts concrete. The classes below are now real extension points rather than pure stubs, so later steps can build on them without reshaping the core wiring.

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

The current skeleton uses `ready() == false` for the indexer, retriever, LLM, and API modules. `Embedder::ready()` returns `true` when the embeddings endpoint configuration is complete. `CppParser::ready()` is wired to the tree-sitter C++ language and returns `true` when the dependency is available. `Storage::ready()` becomes `true` when DuckDB initializes successfully.

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

## Embedder Contract

`qodeloc::core::Embedder` batches snippets of source text and sends them to an OpenAI-compatible embeddings endpoint through `cpp-httplib`:

```cpp
class Embedder final : public IModule {
public:
  using Embedding = std::vector<float>;
  using Embeddings = std::vector<Embedding>;

  struct Options {
    std::string host;
    std::uint16_t port;
    std::string api_path;
    std::string model;
    std::size_t batch_size;
    std::chrono::milliseconds timeout;
  };

  Embedder();
  explicit Embedder(Options options);
  Embedding embed(std::string_view text) const;
  Embeddings embed_batch(std::span<const std::string> texts) const;
};
```

`embed_batch()` groups texts into fixed-size batches to reduce HTTP overhead during primary indexing. The response parser accepts the standard OpenAI `data[]` payload and preserves input order by `index`.

## Indexer Contract

`qodeloc::core::Indexer` walks a repository, parses C++ files, batches embedding requests, and persists the structural graph into `Storage`:

```cpp
class Indexer final : public IModule {
public:
  struct Options {
    std::filesystem::path root_directory;
    std::size_t embedding_batch_size;
    bool recursive;
    std::vector<std::string> source_extensions;
  };

  struct IndexedSymbol {
    SymbolId symbol_id;
    StoredSymbol symbol;
    std::string source_text;
    Embedder::Embedding embedding;
  };

  struct Stats {
    std::size_t files_scanned;
    std::size_t files_indexed;
    std::size_t symbols_indexed;
    std::size_t parse_errors;
    std::size_t embedding_batches;
    std::chrono::milliseconds elapsed;
  };

  struct Result {
    Stats stats;
    std::vector<IndexedSymbol> symbols;
  };

  using EmbeddingBatchFn = std::function<Embedder::Embeddings(std::span<const std::string>)>;

  Indexer();
  explicit Indexer(Options options, const std::filesystem::path& database_path = {},
                   EmbeddingBatchFn embedding_batch = {});
  Result index();
  Result index(const std::filesystem::path& root_directory);
};
```

The current implementation traverses the tree recursively, skips build and VCS directories, parses every file, prepares snippets for embeddings in batches, writes symbols and relationships into DuckDB, and returns progress statistics for reporting. The embedding backend is injectable so tests can run without a live HTTP service.

## Hierarchy Contract

`qodeloc::core::HierarchicalIndex` groups indexed symbols by module, where a module is resolved from the nearest `CMakeLists.txt` when available and otherwise falls back to the first-level directory. It builds a short module summary from header-first public symbols, embeds that summary, and then ranks modules before symbols inside the selected modules.

```cpp
class HierarchicalIndex final {
public:
  struct Options {
    std::size_t module_top_k;
    std::size_t symbol_top_k;
    std::size_t public_symbol_limit;
  };

  struct ModuleRecord {
    std::string module_name;
    std::string module_path;
    std::string summary;
    std::size_t public_symbol_count;
    std::size_t header_count;
    Embedder::Embedding embedding;
  };

  struct ModuleHit {
    ModuleRecord module;
    double score;
  };

  struct SymbolHit {
    Indexer::IndexedSymbol symbol;
    double score;
  };

  struct Result {
    std::vector<ModuleHit> modules;
    std::vector<SymbolHit> symbols;
  };

  using ModuleEmbeddingBatchFn = std::function<Embedder::Embeddings(std::span<const std::string>)>;

  void build(const std::vector<Indexer::IndexedSymbol>& symbols,
             const ModuleEmbeddingBatchFn& module_embedding_batch);
  Result search(const Embedder::Embedding& query_embedding) const;
  std::vector<SymbolHit> search_flat(const Embedder::Embedding& query_embedding) const;
};
```

## Module Map

| Library | Public class | Responsibility |
|---|---|---|
| `libparser` | `qodeloc::core::CppParser` | C++ source parsing and symbol extraction |
| `libindexer` | `qodeloc::core::Indexer`, `qodeloc::core::HierarchicalIndex` | Repository traversal and hierarchical module ranking |
| `libretriever` | `qodeloc::core::Retriever` | Query-time retrieval pipeline |
| `libembedder` | `qodeloc::core::Embedder` | Batched text-to-vector embedding requests |
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
