# Test Data

Fixed C++ fixtures for parser and retriever tests live here.

## Layout

- `parser/` - hand-written C++ fixtures for parser unit tests
- `repos/` - optional downloaded GitHub checkouts used for incremental-update and benchmark-style tests

Use `make download-testdata-repo` to fetch the default public fixture repo (`fmtlib/fmt`) into `testdata/repos/fmt/`.
You can override `TESTDATA_REPO_URL`, `TESTDATA_REPO_REF`, and `TESTDATA_REPO_NAME` if you want to point the same command at a different public repository.
