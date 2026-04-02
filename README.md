# QodeLoc AI Assistant

QodeLoc is an air-gapped AI code assistant for C++ codebases. It stays inside your network perimeter and gives developers semantic search, dependency navigation, local-file-aware explanations, and MCP-based IDE integration.

The full stack now runs in Docker:

- `qdrant` stores symbol vectors
- `jina-embedder` serves the locally downloaded embedding model
- `llama-cpp` serves the locally downloaded generation model
- `litellm` routes model calls and handles fallback
- `core` indexes code, answers HTTP API requests, and manages the dependency graph
- `mcp-adapter` exposes the MCP SSE interface used by Continue.dev and other MCP clients

## Quick Start

1. Download the two local models once:
   - `make install-models-jina-code`
   - `make install-models-llama31-8b`
2. Start the full stack:
   - `make up`
3. Check service health:
   - `make status`
4. Stream logs if needed:
   - `make logs`

`make up` builds the local Docker images for `core`, `mcp-adapter`, and `jina-embedder`, then starts the complete runtime stack.

## What Runs in Docker

| Service | Purpose | Host port |
| --- | --- | --- |
| `qdrant` | Vector store for symbol embeddings | `6333` / `6334` |
| `jina-embedder` | OpenAI-compatible embeddings API | `8081` |
| `llama-cpp` | Local generation backend | `8080` |
| `litellm` | Router and fallback layer | `4000` |
| `core` | C++ API, indexing, retrieval, and graph storage | `3100` |
| `mcp-adapter` | MCP SSE server for IDE clients | `3333` |

## Model Commands

The model installer resolves the Hugging Face CLI from your local `pipx` install. If `hf` is not on `PATH`, it falls back to `~/.local/bin/hf`.

Use these targets to install one model at a time:

- `make install-models-jina-code` - embedding model for code search
- `make install-models-llama31-8b` - lightweight generation model used by the default Docker stack
- `make install-models-codestral2` - Codestral GGUF slot
- `make install-models-qwen3-14b` - 14B Qwen generation model
- `make install-models-qwen3-30b-a3b` - 30B-A3B Qwen generation model
- `make install-models-all` - install the full catalog

Downloaded artifacts stay under `models/downloads/`, and the Hugging Face cache stays under `models/cache/`.

## Core Engine

The `core/` project is a Conan 2 + CMake + Ninja C++23 application. It starts the HTTP API immediately, then boots the initial corpus in the background so `/status` can report `initializing`, `indexing`, or `ready`.

Core uses:

- tree-sitter for AST parsing
- DuckDB for the structural graph
- Qdrant for vector search
- `jina-code` for embeddings
- `llama31-8b` through LiteLLM for generation

Useful local commands:

- `make build` - configure and build the C++ core on the host
- `make test` - run the core test suite through CTest
- `make fmt` - format C++ sources with clang-format
- `make lint` - run clang-tidy over the non-test C++ sources
- `make release` - build the core Docker image

## MCP Adapter

The `mcp-adapter/` project is a TypeScript MCP SSE server that proxies Continue.dev tools to the Core HTTP API.

Useful local commands:

- `make mcp-install`
- `make mcp-build`
- `make mcp-test`
- `make mcp-lint`
- `make mcp-dev`
- `make mcp-release`

## Repository Layout

- `core/` - C++23 core engine
- `mcp-adapter/` - TypeScript MCP server
- `infra/` - Docker Compose stack and container build files
- `models/` - model catalog, install scripts, and notes
- `prompts/` - YAML prompt templates
- `scripts/` - helper scripts for models and fixtures
- `docs/` - architecture, deployment, and API documentation
- `tests/` - integration and end-to-end tests
- `testdata/` - fixed parser and graph fixtures

## Documentation

Start with:

- `docs/docker.md`
- `docs/configuration.md`
- `docs/api.md`
- `docs/api-internal.md`
- `docs/mcp-tools.md`
- `docs/performance.md`
- `docs/QodeLoc_DevPlan.md`

## Development Notes

- `make up-core` and `make down-core` are aliases for the same Docker deployment.
- `make reset` stops the stack and removes volumes.
- `make download-testdata-repo` fetches the default public fixture repo used by the indexer tests.
