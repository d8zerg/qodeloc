# QodeLoc MCP Tools

The `mcp-adapter` exposes the following tools to Continue.dev and other MCP clients.

## Model Aliases

The adapter forwards the optional `model` parameter as the `X-QodeLoc-Model` header on Core HTTP requests.

Available aliases in the LiteLLM router:

- `fast` - short, cheap answers
- `balanced` - default developer-facing route
- `deep` - more deliberate reasoning
- `moe` - fallback-heavy route for larger prompts
- `qodeloc-local` - backward-compatible alias used by the current Core defaults

The adapter defaults to `balanced` when a tool accepts `model` and the caller omits it.

## Tools

### `search_codebase`

- Input: `query`, optional `model`
- Core endpoint: `POST /search`
- Use this for semantic code search over the indexed corpus.

### `get_symbol`

- Input: `name`
- Core endpoint: `POST /explain`
- Use this when you already know the symbol name and want a structured explanation.

### `get_deps`

- Input: `name`, optional `depth`
- Core endpoint: `POST /deps`
- Use this for callers, callees, and transitive module dependencies.

### `explain_symbol`

- Input: `name`, optional `model`, optional `local_files`
- Core endpoint: `POST /explain`
- Use this when the IDE has open or modified files that should be injected into the explanation prompt.

### `find_callers`

- Input: `name`
- Core endpoint: `POST /callers`
- Use this to answer "who calls this symbol?" questions.

### `get_module_overview`

- Input: `module_name`
- Core endpoint: `POST /module`
- Use this for module summaries and internal navigation.

### `sync_workspace_delta`

- Input: `changed_files`, optional `local_files`
- Core endpoint: `POST /reindex`
- Use this to push changed files from the IDE into the Core incremental update pipeline.

