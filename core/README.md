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

## Presets

- `debug`
- `release`
- `relwithdebinfo`

The first binary is a structured-logging stub. Later steps will grow it into the parser/indexer/API service.
