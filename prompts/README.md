# Prompts

YAML prompt templates for the Core LLM layer live here.

## Templates

- `search.yaml`
- `explain.yaml`
- `deps.yaml`
- `callers.yaml`
- `module.yaml`

## How They Are Used

- `PromptBuilder` loads these templates at runtime.
- The templates are parameterized, so prompt behaviour can change without recompiling the core.
- Docker and host builds both read the same prompt directory.

## Rules of Thumb

- Keep prompt text small and explicit.
- Prefer descriptive section names over clever phrasing.
- Update the templates when the retrieval context shape changes.
