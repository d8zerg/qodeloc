# Infrastructure

This directory contains the local development stack and service configuration files.

- `docker-compose.yml` starts Qdrant, LiteLLM, and llama.cpp
- `litellm/config.yaml` configures the proxy model routing
- persistent Docker volumes keep local state between runs
