# Scripts

Helper scripts for indexing, deployment, and model management live here.

## Current Utilities

- `install-models.py` - resolves the local Hugging Face CLI from `pipx`, supports `all` plus the short model names from `models/catalog.json`, and streams download progress from `hf download`.
- `fetch-testdata-repo.py` - clones or updates a public GitHub repository into `testdata/repos/` for integration and incremental-update fixtures.
