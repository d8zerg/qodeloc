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

The parser library is now the first live module: `qodeloc::core::CppParser::parse_file()` uses tree-sitter C++ to extract symbols and their basic dependencies from a source file. The remaining libraries still expose no-op public classes under `include/qodeloc/core/` and are linked into `qodeloc-core`. The internal contract is documented in [`docs/api-internal.md`](../../docs/api-internal.md).

## Presets

- `debug`
- `release`
- `relwithdebinfo`

The first binary is still a structured-logging bootstrap, but it now exercises the real parser module as part of the module graph. Later steps will grow it into the parser/indexer/API service.
