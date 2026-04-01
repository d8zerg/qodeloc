# Infrastructure

This directory contains the local development stack and service configuration files.

- `docker-compose.yml` starts Qdrant, LiteLLM, and the official prebuilt `ghcr.io/ggml-org/llama.cpp:server` image, which is mounted against the locally downloaded `models/downloads/llama31-8b` GGUF file
- `litellm/config.yaml` configures the proxy model routing
- persistent Docker volumes keep local state between runs

If the `llama31-8b` artifact is missing, run `make install-models-llama31-8b` before `make up`.
