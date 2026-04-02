# E2E Fixtures

This directory stores metadata and benchmark fixtures for the full-stack integration tests.

The source checkout for the primary benchmark corpus is downloaded on demand with:

- `make download-e2e-catch2`

Fixture metadata lives under `tests/e2e/fixtures/catch2/`. The downloaded repository itself
is ignored by git and lands in `tests/e2e/fixtures/repos/catch2/`.

