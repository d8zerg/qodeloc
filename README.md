# QodeLoc AI-assistant

QodeLoc is a local code-intelligence monorepo for C++ parsing, retrieval, and MCP integration.

## Bootstrap

1. Check `ENVIRONMENT.md` for the verified toolchain.
2. Run `make up` to start the local dev stack. Docker will pull the official `ghcr.io/ggml-org/llama.cpp:server` image and use the pre-downloaded `models/downloads/llama31-8b/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf` file. If the file is missing, run `make install-models-llama31-8b` first.
3. Run `make status` to inspect service health.

## Model Commands

The model installer resolves the Hugging Face CLI from your local `pipx` install. If `hf` is not on `PATH`, it will fall back to `~/.local/bin/hf` automatically.

Use these targets to install one model at a time:

- `make install-models-jina-code` - download the embedding model used for code search
- `make install-models-llama31-8b` - download the lightweight 8B generation model used by `make up`
- `make install-models-codestral2` - download the Codestral GGUF slot
- `make install-models-qwen3-14b` - download the 14B Qwen GGUF model
- `make install-models-qwen3-30b-a3b` - download the 30B-A3B Qwen GGUF model
- `make install-models-all` - download the full catalog

The CLI shows download progress while each model is fetched. Artifacts land under `models/downloads/<short-name>/`, and the local cache stays in ignored paths under `models/cache/`. The default dev stack reuses the downloaded `llama31-8b` artifact directly and does not fetch it again inside Docker.

If a repo is gated, authenticate first with `hf auth login`.

## Core Engine

`core/` now has a working Conan 2 + CMake + Ninja bootstrap. The first target is a structured-logging stub wired against the `libparser`/`libindexer`/`libretriever`/`libembedder`/`libllm`/`libstorage`/`libapi` skeleton.

Planned workflow:

- install deps: `conan install core -of core/build/debug -s build_type=Debug -s compiler.cppstd=23 -g CMakeDeps -g CMakeToolchain --build=missing`
- configure: `cd core && cmake --preset debug`
- build: `cd core && cmake --build --preset debug`
- run: `./core/build/debug/qodeloc-core`

`make build` wraps the same flow from the repository root.

Core-specific make targets are available alongside that scaffold:

- `make build` - configure and build `core/` once `core/CMakeLists.txt` exists
- `make test` - run the core smoke test through CTest
- `make release` - build the core Docker image from `core/Dockerfile`
- `make up-core` - start the core Docker Compose stack when it is added
- `make down-core` - stop the core Docker Compose stack

## Repository Layout

- `core/` - C++23 core engine
- `mcp-adapter/` - TypeScript MCP server
- `scripts/` - index, deploy, and model helper scripts
- `models/` - model configuration and metadata
- `infra/` - Docker Compose stack and service configs
- `prompts/` - YAML prompt templates
- `docs/` - architecture and operational docs
- `tests/` - integration and end-to-end tests
- `testdata/` - fixed parser fixtures

## Dev Commands

- `make up` - start Qdrant, LiteLLM, and the official prebuilt llama.cpp server image using the local `llama31-8b` model file
- `make logs` - stream container logs
- `make down` - stop the stack
- `make reset` - stop the stack and remove volumes
- `make status` - print compose health and container state
- `make fmt` - run clang-format over C++ sources
- `make lint` - run clang-tidy over C++ sources when compile commands are available
- `make download-testdata-repo` - clone or refresh the default public fixture repo (`fmtlib/fmt`) under `testdata/repos/fmt/`
