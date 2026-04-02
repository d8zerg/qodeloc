# MCP Adapter

The MCP adapter exposes QodeLoc to Continue.dev and other MCP clients over SSE.

## What It Does

- Authenticates clients with bearer API keys
- Proxies MCP tools to the Core HTTP API
- Normalizes `local_files` and `changed_files` against the workspace root
- Supports model aliases such as `fast`, `balanced`, `deep`, and `moe`
- Provides a `sync_workspace_delta` tool for feature-branch workflows

## Development Commands

- `make mcp-install` - install dependencies
- `make mcp-build` - type-check and compile the TypeScript sources
- `make mcp-test` - run the adapter tests
- `make mcp-lint` - run ESLint
- `make mcp-dev` - run the SSE server in development mode
- `make mcp-bundle` - produce the production bundle
- `make mcp-start` - start the bundled server
- `make mcp-release` - build the Docker image

## Configuration

By default the adapter reads `mcp-adapter/config.json`.
Environment variables can override any important runtime value:

- `QODELOC_MCP_HOST`
- `QODELOC_MCP_PORT`
- `QODELOC_MCP_API_KEYS`
- `QODELOC_CORE_API_URL`
- `QODELOC_MCP_WORKSPACE_ROOT`

## Docker

The Docker image bundles the TypeScript build and runs the SSE server against the mounted repository checkout.
