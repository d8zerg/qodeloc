# Configuration

QodeLoc reads its runtime settings from `.env` plus component-specific environment overrides.
When the stack runs in Docker, the compose file overrides the service hostnames so each container talks to the other containers instead of `127.0.0.1`.

## Core Runtime

| Variable | Purpose |
| --- | --- |
| `QODELOC_EMBEDDER_HOST` | Embedding endpoint host |
| `QODELOC_EMBEDDER_PORT` | Embedding endpoint port |
| `QODELOC_EMBEDDER_API_PATH` | OpenAI-compatible embeddings path |
| `QODELOC_EMBEDDER_MODEL` | Embedding model alias sent to the endpoint |
| `QODELOC_LLM_HOST` | LiteLLM host |
| `QODELOC_LLM_PORT` | LiteLLM port |
| `QODELOC_LLM_API_PATH` | Chat completions path |
| `QODELOC_LLM_MODEL` | Default generation model alias |
| `QODELOC_LLM_API_KEY` | Bearer token sent to LiteLLM |
| `QODELOC_PROMPTS_DIR` | YAML prompt template directory |
| `QODELOC_PROMPT_*` | Prompt section and token budgets |
| `QODELOC_HIERARCHY_*` | Module-ranking limits |
| `QODELOC_RETRIEVER_*` | Retrieval context limits |
| `QODELOC_API_*` | Core HTTP API host, port, and request limits |
| `QODELOC_INDEXER_*` | Index traversal settings |
| `QODELOC_STORAGE_DB_PATH` | DuckDB file path |
| `QODELOC_GIT_BASE_REF` | Default base ref for incremental updates |

## Qdrant Vector Store

| Variable | Purpose |
| --- | --- |
| `QODELOC_QDRANT_ENABLED` | Enables the Qdrant-backed vector store |
| `QODELOC_QDRANT_HOST` | Qdrant host |
| `QODELOC_QDRANT_PORT` | Qdrant HTTP port |
| `QODELOC_QDRANT_COLLECTION` | Collection name used for symbol vectors |
| `QODELOC_QDRANT_VECTOR_SIZE` | Expected embedding dimension |
| `QODELOC_QDRANT_TIMEOUT_MS` | HTTP timeout for Qdrant requests |
| `QODELOC_QDRANT_API_KEY` | Optional API key for protected Qdrant deployments |

## MCP Adapter

| Variable | Purpose |
| --- | --- |
| `QODELOC_MCP_HOST` | SSE server host |
| `QODELOC_MCP_PORT` | SSE server port |
| `QODELOC_MCP_PATH` | SSE handshake path |
| `QODELOC_MCP_MESSAGES_PATH` | MCP messages endpoint |
| `QODELOC_MCP_HEALTH_PATH` | Health endpoint |
| `QODELOC_MCP_API_KEYS` | Comma-separated bearer tokens |
| `QODELOC_CORE_API_URL` | Core HTTP API base URL |
| `QODELOC_CORE_API_TIMEOUT_MS` | HTTP timeout for Core calls |
| `QODELOC_CORE_MODEL_HEADER` | Header used to pass the model alias |
| `QODELOC_MCP_DEFAULT_MODEL` | Default model alias used by MCP tools |
| `QODELOC_MCP_WORKSPACE_ROOT` | Workspace root for `local_files` normalization |
| `QODELOC_MCP_MAX_LOCAL_FILES` | Maximum IDE files accepted in a single tool call |
| `QODELOC_MCP_MAX_CHANGED_FILES` | Maximum changed files accepted by workspace sync |

## Docker Stack Overrides

The compose stack pins the runtime hostnames internally:

- `jina-embedder`
- `qdrant`
- `llama-cpp`
- `litellm`
- `core`
- `mcp-adapter`

This keeps container-to-container traffic inside the compose network while still exposing the usual host ports for debugging.

## Practical Defaults

- Use `jina-code` for embeddings in the Docker stack.
- Use `llama31-8b` for the default local generation model.
- Docker Compose sets `QODELOC_QDRANT_ENABLED=true` for `core`, so the retrieval path uses Qdrant in the full stack.
- Leave `QODELOC_QDRANT_ENABLED=false` outside Docker if you only want the in-memory fallback path during lightweight local tests.
