# Core

The C++ core engine indexes the repository, stores graph relationships in DuckDB, and now uses Qdrant as the vector store for symbol search.

## Responsibilities

- Parse C++ sources with tree-sitter
- Extract symbol and dependency information
- Generate embeddings through the configured embedding endpoint
- Persist the structural graph in DuckDB
- Persist symbol vectors in Qdrant
- Expose the HTTP API used by the MCP adapter and direct clients
- Render prompts and call LiteLLM for generation

## Runtime Modes

- Default mode starts the HTTP API server and runs the initial index in the background.
- `--smoke` keeps the fast module-graph self-check.

## Configuration

Runtime defaults are loaded from the repository `.env` plus Docker overrides through `qodeloc::core::Config`.
Important pieces:

- `QODELOC_EMBEDDER_*` - embedding endpoint
- `QODELOC_LLM_*` - LiteLLM endpoint
- `QODELOC_QDRANT_*` - vector store settings
- `QODELOC_PROMPTS_DIR` - prompt templates
- `QODELOC_API_*` - HTTP API host, port, and limits
- `QODELOC_STORAGE_DB_PATH` - DuckDB file path

## Local Build

```bash
conan install core -of core/build/debug -s build_type=Debug -s compiler.cppstd=23 -g CMakeDeps -g CMakeToolchain --build=missing
cd core
cmake --preset debug
cmake --build --preset debug
./build/debug/qodeloc-core
```

## Tests

- `qodeloc-parser-tests`
- `qodeloc-storage-tests`
- `qodeloc-embedder-tests`
- `qodeloc-llm-tests`
- `qodeloc-prompt-builder-tests`
- `qodeloc-api-tests`
- `qodeloc-indexer-tests`
- `qodeloc-indexer-update-tests`
- `qodeloc-hierarchy-tests`
- `qodeloc-retriever-tests`

## Docker

`core/Dockerfile` builds the service image used by the compose stack. The container runs against the mounted workspace so the index reflects the current checkout.
