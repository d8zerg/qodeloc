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

## Config Contract

`qodeloc::core::Config` reads `.env` files and process environment variables, then hands the resolved settings to the module default constructors:

```cpp
class Config final {
public:
  static Config load(const std::filesystem::path& env_file = {});
  static const Config& current();

  Embedder::Options embedder_options() const;
  LlmClient::Options llm_options() const;
  PromptBuilder::Options prompt_builder_options() const;
  HierarchicalIndex::Options hierarchy_options() const;
  Retriever::Options retriever_options() const;
  ApiServer::Options api_options() const;
  Indexer::Options indexer_options(const std::filesystem::path& root_directory = {}) const;
  GitWatcher::Options git_watcher_options(
      const std::filesystem::path& repository_root = {}) const;
  std::filesystem::path storage_database_path() const;
  std::string git_base_ref() const;
};
```

`Config::current()` searches upward from the current working directory for `qodeloc/.env`, so tests and local binaries launched from `core/build/debug` still pick up the repository defaults. Relative paths such as `QODELOC_PROMPTS_DIR=prompts` are resolved against the config file directory when the file is found.

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
  void begin_transaction();
  void commit_transaction();
  void rollback_transaction();
  SymbolId write_symbol(const StoredSymbol& symbol);
  std::vector<SymbolId> write_symbols(std::span<const StoredSymbol> symbols);
  void write_call(SymbolId caller_id, SymbolId callee_id);
  void write_calls(std::span<const CallEdge> calls);
  void write_include(SymbolId source_id, std::string include_path, std::string target_module_name = {});
  void write_includes(std::span<const IncludeEdge> includes);
  void write_inheritance(SymbolId derived_id, SymbolId base_id);
  void write_inheritances(std::span<const InheritanceEdge> inheritances);
  std::vector<StoredSymbol> callers_of(SymbolId symbol_id) const;
  std::vector<StoredSymbol> callees_from(SymbolId symbol_id) const;
  std::vector<ModuleDependency> transitive_module_dependencies(std::string_view module_name,
                                                               std::size_t max_depth) const;
  void delete_file(std::string_view file_path);
  void delete_files(std::span<const std::filesystem::path> file_paths);
};
```

The schema uses five tables:

- `symbols` for symbol metadata and file/module membership
- `calls` for symbol-to-symbol call edges
- `includes` for header dependencies
- `inheritance` for base/derived class edges
- `modules` for module and directory bookkeeping

`DependencyGraph` now exposes batched mutation helpers so the indexer can keep a single DuckDB transaction open while deleting stale files, inserting symbols, and writing the three edge tables. Single-row helpers remain available as thin wrappers for tests and small tooling.

`Storage` is the thin `IModule` wrapper around `DependencyGraph`, so later phases can depend on the same storage layer without reworking the core bootstrap.

## Git Watcher Contract

`qodeloc::core::GitWatcher` shells out to `git diff --name-only <base_ref>` for the repository root and returns the changed paths as `std::filesystem::path` values. `Indexer::update_from_git()` wraps that helper and passes the resulting paths into the existing incremental update pipeline. The default `base_ref` comes from `Config`.

## Embedder Contract

`qodeloc::core::Embedder` batches snippets of source text and sends them to an OpenAI-compatible embeddings endpoint through Boost.Beast/Asio:

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

## LLM Contract

`qodeloc::core::LlmClient` uses Boost.Beast to talk to the local LiteLLM router over the OpenAI-compatible chat/completions API:

```cpp
class LlmClient final : public IModule {
public:
  struct ChatMessage {
    std::string role;
    std::string content;
  };

  struct Options {
    std::string host;
    std::uint16_t port;
    std::string api_path;
    std::string model;
    std::string api_key;
    std::chrono::milliseconds timeout;
    std::size_t max_retries;
    std::chrono::milliseconds initial_backoff;
    std::chrono::milliseconds max_backoff;
  };

  struct ChatRequest {
    std::vector<ChatMessage> messages;
    std::string model;
    bool stream;
    std::optional<float> temperature;
    std::optional<std::size_t> max_tokens;
    std::optional<float> top_p;
  };

  struct ChatResponse {
    std::string content;
    nlohmann::json raw;
  };

  using StreamCallback = std::function<bool(std::string_view)>;

  ChatResponse complete(const ChatRequest& request) const;
  ChatResponse stream(const ChatRequest& request, StreamCallback on_chunk = {}) const;
};
```

The client retries transient transport failures and 429/5xx responses with exponential backoff. Non-streaming requests return the final assistant content, while streaming requests collect SSE `delta.content` chunks and optionally forward each chunk to the caller.

## Prompt Builder Contract

`qodeloc::core::PromptBuilder` renders YAML templates from `prompts/` into a two-message chat prompt. The default template directory comes from `Config` and is usually `qodeloc/prompts/`:

```cpp
class PromptBuilder {
public:
  enum class RequestType {
    Search,
    Explain,
    Deps,
    Callers,
    Module,
  };

  struct LocalFile {
    std::filesystem::path path;
    std::string content;
  };

  struct RenderedPrompt {
    std::string template_name;
    std::size_t context_token_limit;
    std::size_t token_count;
    std::string system_text;
    std::string user_text;
    std::vector<LlmClient::ChatMessage> messages;
  };

  RenderedPrompt build(RequestType request_type, std::string_view query,
                       const Retriever::Result& retrieval,
                       std::span<const LocalFile> local_files = {}) const;
};
```

The builder loads a small YAML subset with `name`, `context_token_limit`, `system`, and `user` keys. It substitutes retrieval summaries, local file snippets, and query metadata, then trims the result to the configured token budget.

## Indexer Contract

`qodeloc::core::Indexer` walks a repository, parses C++ files, batches embedding requests, and persists the structural graph into `Storage`. Its default batching and file-extension settings come from `Config`:

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
  Result update(const std::vector<std::filesystem::path>& changed_files);
  Result update_from_git(std::string_view base_ref = {});
};
```

The current implementation traverses the tree recursively, skips build and VCS directories, parses every file, prepares snippets for embeddings in batches, writes symbols and relationships into DuckDB, and returns progress statistics for reporting. The embedding backend is injectable so tests can run without a live HTTP service.

## Hierarchy Contract

`qodeloc::core::HierarchicalIndex` groups indexed symbols by module, where a module is resolved from the nearest `CMakeLists.txt` when available and otherwise falls back to the first-level directory. It builds a short module summary from header-first public symbols, embeds that summary, and then ranks modules before symbols inside the selected modules. Its top-k and public-symbol limits are now config-driven.

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

## Retriever Contract

`qodeloc::core::Retriever` is the query-time wrapper around embedding, hierarchical ranking, and DuckDB context expansion. It currently uses the in-memory `HierarchicalIndex` as the ANN boundary and enriches each hit with direct callers and callees from `Storage`.

```cpp
class Retriever final : public IModule {
public:
  struct Options {
    HierarchicalIndex::Options hierarchy;
    std::size_t related_symbol_limit;
    std::size_t context_token_limit;
  };

  struct SymbolContext {
    Indexer::IndexedSymbol symbol;
    double score;
    std::vector<StoredSymbol> callers;
    std::vector<StoredSymbol> callees;
    std::string context;
    std::size_t token_count;
  };

  struct Result {
    std::string query;
    Embedder::Embedding query_embedding;
    std::vector<HierarchicalIndex::ModuleHit> modules;
    std::vector<SymbolContext> symbols;
  };

  using QueryEmbeddingFn = std::function<Embedder::Embedding(std::string_view)>;

  void attach_storage(const Storage& storage) noexcept;
  void build(const std::vector<Indexer::IndexedSymbol>& symbols,
             const HierarchicalIndex::ModuleEmbeddingBatchFn& module_embedding_batch = {});
  Result retrieve(std::string_view query) const;
  Result retrieve(const Embedder::Embedding& query_embedding, std::string_view query = {}) const;
};
```

`Retriever::build()` loads the current corpus, `Retriever::attach_storage()` connects the DuckDB graph, and `retrieve()` embeds the query, performs hierarchical ranking, then appends a short graph context. The token budget is enforced approximately by whitespace token counting so the assembled context stays bounded before the later prompt-building stage.

## API Contract

`qodeloc::core::ApiServer` exposes the HTTP façade for the core engine. It uses Boost.Beast, binds to the configured host and port, and serves JSON endpoints for search, explanations, dependencies, status, and incremental reindexing.

```cpp
class ApiServer final : public IModule {
public:
  struct Options {
    std::string host;
    std::uint16_t port;
    std::size_t max_body_bytes;
    std::chrono::milliseconds request_timeout;
  };

  struct Status {
    bool running;
    std::string host;
    std::uint16_t port;
    std::filesystem::path root_directory;
    std::size_t symbol_count;
    std::size_t module_count;
    std::size_t indexed_files;
    Indexer::Stats last_stats;
    std::chrono::system_clock::time_point last_indexed_at;
    std::string last_operation;
    bool retriever_ready;
    bool llm_ready;
  };

  void attach_indexer(Indexer& indexer) noexcept;
  void attach_retriever(Retriever& retriever) noexcept;
  void attach_prompt_builder(PromptBuilder& prompt_builder) noexcept;
  void attach_llm_client(LlmClient& llm_client) noexcept;
  void attach_module_embedding_batch(Retriever::ModuleEmbeddingBatchFn module_embedding_batch);

  void start();
  void stop() noexcept;
  Status status() const;
};
```

The server answers `POST /search`, `POST /explain`, `POST /deps`, `POST /callers`, `POST /module`, and `POST /reindex`, plus `GET /status`. Each route consumes and emits JSON. The `reindex` handler accepts either an explicit `changed_files` list or falls back to `git diff` through `GitWatcher`.

## Module Map

| Library | Public class | Responsibility |
|---|---|---|
| `libparser` | `qodeloc::core::CppParser` | C++ source parsing and symbol extraction |
| `libindexer` | `qodeloc::core::Indexer`, `qodeloc::core::HierarchicalIndex` | Repository traversal and hierarchical module ranking |
| `libretriever` | `qodeloc::core::Retriever` | Query-time retrieval pipeline |
| `libembedder` | `qodeloc::core::Embedder` | Batched text-to-vector embedding requests |
| `libllm` | `qodeloc::core::LlmClient`, `qodeloc::core::PromptBuilder` | OpenAI-compatible LLM client and prompt rendering |
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
