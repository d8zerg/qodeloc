# Tests

This directory contains integration and end-to-end checks for the QodeLoc stack.

## Layout

- `e2e/` - Python end-to-end runner, smoke-load driver, fixtures, and test reports
- `e2e/fixtures/` - frozen queries and repository fixtures used for baseline comparisons

## Commands

- `make e2e-test` - run the automated retrieval and MCP baseline
- `make e2e-load-test` - run the smoke-load scenario with three parallel clients
- `make download-e2e-catch2` - refresh the public C++ fixture repo used by the e2e suite

## Notes

- The e2e suite can run against the local Docker stack or against a host-built core and MCP adapter.
- The smoke-load scenario intentionally stays lightweight so it can be used repeatedly during development.
