# QodeLoc AI-assistant

QodeLoc is a local code-intelligence monorepo for C++ parsing, retrieval, and MCP integration.

## Bootstrap

1. Check `ENVIRONMENT.md` for the verified toolchain.
2. Run `make up` to start the local dev stack.
3. Run `make status` to inspect service health.

## Core Engine

`core/` is still a scaffold placeholder. The build and launch flow will be added in step 1.1 of [docs/QodeLoc_DevPlan.md](/home/dsb/repos/github/d8zerg/qodeloc-workspace/docs/QodeLoc_DevPlan.md).

Planned workflow:

- configure: `cmake --preset debug`
- build: `cmake --build --preset debug`
- run: `./build/debug/qodeloc-core`

The initial binary will be a structured-logging stub; later steps will grow it into the parser/indexer/API service.

Core-specific make targets will live alongside that scaffold:

- `make build` - configure and build `core/` once `core/CMakeLists.txt` exists
- `make test` - run CTest for the core build tree
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

- `make up` - start Qdrant, LiteLLM, and llama.cpp
- `make logs` - stream container logs
- `make down` - stop the stack
- `make reset` - stop the stack and remove volumes
- `make status` - print compose health and container state
- `make fmt` - run clang-format over C++ sources
- `make lint` - run clang-tidy over C++ sources when compile commands are available
