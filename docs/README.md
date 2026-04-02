# Docs

This directory collects the operational and architectural documentation for QodeLoc.

## Start Here

- `docker.md` - full Docker deployment, services, ports, volumes, and compose workflow.
- `configuration.md` - environment variables and runtime knobs for the full stack.
- `api.md` - OpenAPI 3.1 spec for the Core HTTP API.
- `api-internal.md` - internal C++ module contracts and implementation notes.
- `mcp-tools.md` - MCP tools and model-routing conventions.
- `performance.md` - baseline latency and retrieval measurements.
- `QodeLoc_DevPlan.md` - the step-by-step implementation plan.

## Operational Notes

- The Docker stack is the primary way to run the whole system locally.
- Core starts its HTTP API immediately and finishes the first corpus index in the background.
- `QodeLoc_DevPlan.md` is kept in sync with the implementation status so the plan can be used as a delivery checklist.
