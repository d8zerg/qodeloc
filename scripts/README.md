# Scripts

Helper scripts for model management and fixture downloads live here.

## Available Scripts

- `install-models.py` - downloads the catalog entries from Hugging Face and shows progress in the terminal
- `fetch-testdata-repo.py` - clones or refreshes a public fixture repository into `testdata/repos/`

## Make Targets

- `make install-models-<short-name>`
- `make install-models-all`
- `make download-testdata-repo`
- `make download-e2e-catch2`

## Notes

- The model installer expects the Hugging Face CLI from `pipx` (`hf` or `huggingface-cli`).
- Fixture repositories are intentionally placed under ignored paths so they can be refreshed locally without polluting the repository history.
