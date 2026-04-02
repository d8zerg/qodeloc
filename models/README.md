# Models

This directory stores the model catalog, install notes, and downloaded artifacts used by the Docker stack.

## Catalog

[`catalog.json`](./catalog.json) maps the short names used by `make install-models-*` to Hugging Face repositories and local artifact paths.

| Short name | Kind | Used by | Approx. VRAM | Notes |
| --- | --- | --- | --- | --- |
| `jina-code` | embedding | `jina-embedder`, `core` | ~1 GB | Default code embedding model for search and retrieval |
| `llama31-8b` | generation | `llama-cpp`, `litellm`, `core` | ~6-8 GB | Default lightweight generation model for local development |
| `codestral2` | generation | optional | ~16-24 GB | Codestral GGUF slot |
| `qwen3-14b` | generation | optional | ~12-16 GB | Mid-sized Qwen model |
| `qwen3-30b-a3b` | generation | optional | ~24-32 GB | Larger MoE Qwen model |

## Commands

- `make install-models-jina-code`
- `make install-models-llama31-8b`
- `make install-models-codestral2`
- `make install-models-qwen3-14b`
- `make install-models-qwen3-30b-a3b`
- `make install-models-all`

The installer resolves the local Hugging Face CLI from `pipx` and streams download progress directly in the terminal.

## Docker Notes

- `jina-embedder` mounts `models/downloads/jina-code/`
- `llama-cpp` mounts `models/downloads/llama31-8b/`
- The Docker stack does not re-download models inside the containers
