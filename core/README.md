# Core

QodeLoc is an air-gapped AI code assistant for teams that cannot send source code to the cloud. It runs entirely on your own infrastructure and provides semantic search, dependency navigation, and natural-language explanations of large C++ codebases - all from within your IDE.
Developers connect through Continue.dev in VSCode or CLion. The system indexes the repository using AST-level parsing via tree-sitter, stores symbol vectors in Qdrant, and maintains a dependency graph in DuckDB. Queries are answered by a locally running language model through llama.cpp, routed through LiteLLM. No code, query, or response leaves the network perimeter.

## Bootstrap

From the repository root:

```bash
conan install core -of core/build/debug -s build_type=Debug -s compiler.cppstd=23 -g CMakeDeps -g CMakeToolchain --build=missing
cd core
cmake --preset debug
cmake --build --preset debug
./build/debug/qodeloc-core
```

The same flow is wrapped by `make build` from the repository root.

## Module Skeleton

Phase 1.2 splits the core into static libraries:

- `libparser`
- `libindexer`
- `libretriever`
- `libembedder`
- `libllm`
- `libstorage`
- `libapi`

The parser library is live: `qodeloc::core::CppParser::parse_file()` uses tree-sitter C++ to extract symbols and their basic dependencies from a source file. `libindexer` is now live too: `qodeloc::core::Indexer` walks a repository, batches embeddings, and writes symbol relationships into storage, while `qodeloc::core::HierarchicalIndex` builds module summaries and module-first ranking over the indexed symbol corpus. `libstorage` is also live now: `qodeloc::core::DependencyGraph` stores the DuckDB schema for symbols, calls, includes, inheritance, and modules, and `qodeloc::core::Storage::graph()` exposes that backend to later phases. `libembedder` now speaks to a configurable OpenAI-compatible embeddings endpoint and batches requests before dispatch. The internal contract is documented in [`docs/api-internal.md`](../docs/api-internal.md).

## Presets

- `debug`
- `release`
- `relwithdebinfo`

The first binary is still a structured-logging bootstrap, but it now exercises the real parser, indexer, embedder, and storage modules as part of the module graph. Later steps will grow it into the retrieval and API layers.
