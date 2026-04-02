# Docker Deployment

QodeLoc is designed to run as a single Docker Compose stack for day-to-day development and testing.
The stack now includes every runtime component:

- `qdrant` - vector store for symbol embeddings
- `jina-embedder` - OpenAI-compatible embeddings API backed by the locally downloaded `jina-code` model
- `llama-cpp` - prebuilt generation server image with the locally downloaded `llama31-8b` GGUF model
- `litellm` - routing layer for generation models
- `core` - C++ API, indexer, retriever, storage runtime, and Qdrant sync/search logic
- `mcp-adapter` - SSE MCP server for Continue.dev / MCP clients

## Quick Start

1. Download the models once:
   - `make install-models-jina-code`
   - `make install-models-llama31-8b`
2. Start the full stack:
   - `make up`
3. Check status:
   - `make status`
4. Tail logs:
   - `make logs`

The first `make up` will also build the local `core`, `mcp-adapter`, and `jina-embedder` images.

## Service Topology

| Service | Purpose | Internal port | Host port |
| --- | --- | --- | --- |
| `qdrant` | Vector store for symbols | `6333` / `6334` | `6333` / `6334` |
| `jina-embedder` | Embedding API for code search | `8081` | `8081` |
| `llama-cpp` | Local generation backend | `8080` | `8080` |
| `litellm` | Model router and fallback layer | `4000` | `4000` |
| `core` | HTTP API, indexing, retrieval, and graph storage | `3100` | `3100` |
| `mcp-adapter` | MCP SSE transport and tool routing | `3333` | `3333` |

## Persistent Data

- `qdrant_storage` keeps the vector database between runs.
- `qodeloc_core_storage` keeps the DuckDB graph state between runs.
- `models/downloads/` is mounted read-only into the inference containers.
- When `core` starts in the Docker stack, it enables Qdrant-backed search and writes symbol vectors into the `qdrant` service after each successful index pass.

## Health Semantics

- `core` reports `initializing` or `indexing` while the first corpus bootstrap is still running.
- `/status` returns `bootstrap_state`, `bootstrap_message`, symbol counts, and last indexing metadata.
- `mcp-adapter` is healthy when its SSE health endpoint is reachable.

## Useful Commands

- `make down` - stop the whole stack
- `make reset` - stop the stack and remove volumes
- `make up-core` - alias for the same Docker deployment when you want to emphasise the core runtime
- `make down-core` - alias for stopping the same stack

## Troubleshooting

- If `make up` complains about missing models, re-run the `make install-models-*` command for that short name.
- If the embedder fails to load, verify that `models/downloads/jina-code/` exists and contains the downloaded Hugging Face files.
- If Qdrant starts but `core` fails on boot, check the `QODELOC_QDRANT_*` environment values and the `core` logs.
- If retrieval looks stale after editing code, confirm that `core` has finished the background bootstrap and that `qodeloc_qdrant_storage` is not accidentally wiped.
- If `mcp-adapter` cannot reach Core, confirm that `QODELOC_CORE_API_URL` points at `http://core:3100` inside the compose stack.
