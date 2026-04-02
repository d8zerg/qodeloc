# QodeLoc Performance Baseline

## LiteLLM Load Smoke

- Command: `make e2e-load-test`
- Scenario: 3 parallel clients, 5-10 second intervals, 2 minutes
- Test max_tokens: 32
- Model alias: `balanced`
- Backend: local `llama.cpp` through LiteLLM

### Result

- Requests: 24
- Success: 24
- Errors: 0
- Elapsed: 126.0s
- p50 latency: 8133.3 ms
- p95 latency: 12359.1 ms
- p99 latency: 12359.4 ms

### Notes

- The smoke baseline is intentionally small for local development.
- The same script supports longer soak runs via `--duration-seconds`, higher concurrency via `--workers`, and a different generation budget via `--max-tokens`.
- Keep the local router and llama backend healthy before running the test.
