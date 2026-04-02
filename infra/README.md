# Infrastructure

This directory contains the Docker deployment for the full QodeLoc stack.

## Services

- `docker-compose.yml` starts the complete runtime stack:
  - `qdrant`
  - `jina-embedder`
  - `llama-cpp`
  - `litellm`
  - `core`
  - `mcp-adapter`
- `litellm/config.yaml` configures model routing and fallback.
- `embeddings/server.py` serves the local embedding endpoint for the downloaded `jina-code` model.

## Docker Workflow

- `make up` - build and start the full stack
- `make logs` - tail container logs
- `make status` - print container and health status
- `make down` - stop the stack
- `make reset` - stop the stack and remove persisted volumes

## Notes

- The runtime stack is designed around the locally downloaded `jina-code` and `llama31-8b` artifacts.
- `core` and `mcp-adapter` are built from the repository source and run against the mounted workspace.
- `qdrant` keeps the vector index persistent between runs.
