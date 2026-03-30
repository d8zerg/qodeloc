# Core

The C++23 core engine for parsing, indexing, retrieval, and API serving lives here.

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

The parser library is live: `qodeloc::core::CppParser::parse_file()` uses tree-sitter C++ to extract symbols and their basic dependencies from a source file. `libstorage` is also live now: `qodeloc::core::DependencyGraph` stores the DuckDB schema for symbols, calls, includes, inheritance, and modules, and `qodeloc::core::Storage::graph()` exposes that backend to later phases. The internal contract is documented in [`docs/api-internal.md`](../docs/api-internal.md).

## Presets

- `debug`
- `release`
- `relwithdebinfo`

The first binary is still a structured-logging bootstrap, but it now exercises the real parser and storage modules as part of the module graph. Later steps will grow it into the parser/indexer/API service.
