# Prompts

Prompt templates are stored here as a small YAML subset that is easy to diff and stable for local development.
`qodeloc::core::Config` resolves this directory from `QODELOC_PROMPTS_DIR` in `.env`, so the runtime path stays in sync with the rest of the local settings.

## Supported Keys

- `name`
- `context_token_limit`
- `system`
- `user`

The `system` and `user` values are block scalars, so multi-line instructions can stay readable.

## Placeholders

The `PromptBuilder` replaces these placeholders at runtime:

- `{{request_type}}`
- `{{query}}`
- `{{context_token_limit}}`
- `{{module_count}}`
- `{{symbol_count}}`
- `{{local_file_count}}`
- `{{modules}}`
- `{{symbols}}`
- `{{local_files}}`

## Templates

- `search.yaml`
- `explain.yaml`
- `deps.yaml`
- `callers.yaml`
- `module.yaml`

Each template keeps the final prompt bounded so the local `llama31-8b` stack can stay within the configured context window.
