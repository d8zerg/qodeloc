# Models

Model configuration files, manifests, and sizing notes live here.

## Catalog

The source of truth is [`catalog.json`](./catalog.json), which maps the short install names used by `make install-models-*` to Hugging Face repositories.

| Short name | Hugging Face repo | Artifact | Approx. VRAM | Notes |
| --- | --- | --- | --- | --- |
| `jina-code` | `jinaai/jina-embeddings-v2-base-code` | full repo | ~1 GB | Embedding model for code search and semantic retrieval |
| `llama31-8b` | `bartowski/Meta-Llama-3.1-8B-Instruct-GGUF` | `Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf` | ~6-8 GB | Minimal generation model for constrained GPU setups and the default local stack model |
| `codestral2` | `bartowski/Codestral-22B-v0.1-GGUF` | `Codestral-22B-v0.1-Q4_K_M.gguf` | ~16-24 GB | Historical `codestral2` slot backed by the published Codestral 22B GGUF build |
| `qwen3-14b` | `ggml-org/Qwen3-14B-GGUF` | `Qwen3-14B-Q4_K_M.gguf` | ~12-16 GB | Mid-sized generation model |
| `qwen3-30b-a3b` | `Qwen/Qwen3-30B-A3B-GGUF` | `Qwen3-30B-A3B-Q4_K_M.gguf` | ~24-32 GB | Larger MoE generation model |

## Commands

- `make install-models-jina-code`
- `make install-models-llama31-8b`
- `make install-models-codestral2`
- `make install-models-qwen3-14b`
- `make install-models-qwen3-30b-a3b`
- `make install-models-all`

The installer resolves the local Hugging Face CLI from `pipx` and streams progress from `hf download` directly in the terminal.

Downloaded artifacts and the Hugging Face cache stay under ignored paths in `models/downloads/` and `models/cache/`. The default Docker dev stack reuses the local `llama31-8b` GGUF artifact directly instead of downloading a fresh copy in the container.
