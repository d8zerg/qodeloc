# Infrastructure

This directory contains the local development stack and service configuration files.

- `docker-compose.yml` starts Qdrant, LiteLLM, and a locally built Ubuntu-based llama.cpp server image
- `llama-cpp/Dockerfile` builds the llama.cpp server inside Ubuntu 24.04
- `litellm/config.yaml` configures the proxy model routing
- persistent Docker volumes keep local state between runs
