from __future__ import annotations

import argparse
import json
import random
import statistics
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


DEFAULT_BASE_URL = 'http://127.0.0.1:4000'
DEFAULT_MODEL = 'balanced'
DEFAULT_API_KEY = 'sk-qodeloc-dev'


def wait_for_readiness(url: str, timeout_seconds: float = 180.0) -> None:
  started = time.monotonic()
  last_error = ''

  while time.monotonic() - started < timeout_seconds:
    try:
      with urlopen(url, timeout=10.0) as response:
        payload = json.loads(response.read().decode('utf-8'))
        if isinstance(payload, dict) and payload.get('status') in {'ok', 'ready', 'healthy'}:
          return
        last_error = f'Unexpected readiness payload: {payload!r}'
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
      last_error = str(exc)

    time.sleep(1.0)

  raise RuntimeError(f'Timed out waiting for LiteLLM readiness: {last_error}')


def percentile(values: list[float], quantile: float) -> float:
  if not values:
    return 0.0
  ordered = sorted(values)
  index = int(round((len(ordered) - 1) * quantile))
  index = max(0, min(index, len(ordered) - 1))
  return ordered[index]


@dataclass(slots=True)
class RequestResult:
  latency_ms: float
  ok: bool
  status: int
  error: str = ''


def send_request(
    base_url: str,
    api_key: str,
    model: str,
    prompt: str,
    max_tokens: int,
) -> RequestResult:
  payload = {
      'model': model,
      'messages': [
          {
              'role': 'user',
              'content': prompt,
          }
      ],
      'temperature': 0.2,
      'max_tokens': max_tokens,
      'stream': False,
  }

  request = Request(
      f'{base_url.rstrip("/")}/v1/chat/completions',
      data=json.dumps(payload).encode('utf-8'),
      headers={
          'Authorization': f'Bearer {api_key}',
          'Content-Type': 'application/json',
          'Accept': 'application/json',
      },
      method='POST',
  )

  started = time.perf_counter()
  try:
    with urlopen(request, timeout=120.0) as response:
      response.read()
      elapsed = (time.perf_counter() - started) * 1000.0
      return RequestResult(latency_ms=elapsed, ok=True, status=response.status)
  except HTTPError as exc:
    elapsed = (time.perf_counter() - started) * 1000.0
    error_text = ''
    try:
      error_text = exc.read().decode('utf-8')
    except Exception:
      error_text = exc.reason if isinstance(exc.reason, str) else str(exc.reason)
    return RequestResult(latency_ms=elapsed, ok=False, status=exc.code, error=error_text)
  except (URLError, TimeoutError, OSError) as exc:
    elapsed = (time.perf_counter() - started) * 1000.0
    return RequestResult(latency_ms=elapsed, ok=False, status=0, error=str(exc))


def worker_loop(
    worker_id: int,
    base_url: str,
    api_key: str,
    model: str,
    prompt: str,
    duration_seconds: float,
    min_interval_seconds: float,
    max_interval_seconds: float,
    requests_per_worker: int,
    max_tokens: int,
) -> list[RequestResult]:
  results: list[RequestResult] = []
  deadline = time.monotonic() + duration_seconds if duration_seconds > 0 else None

  for request_index in range(requests_per_worker):
    if deadline is not None and time.monotonic() >= deadline:
      break

    result = send_request(
        base_url,
        api_key,
        model,
        f'[{worker_id}:{request_index}] {prompt}',
        max_tokens,
    )
    results.append(result)

    if deadline is None:
      continue

    if time.monotonic() >= deadline:
      break

    sleep_seconds = random.uniform(min_interval_seconds, max_interval_seconds)
    remaining = deadline - time.monotonic()
    time.sleep(max(0.0, min(sleep_seconds, remaining)))

  while deadline is not None and time.monotonic() < deadline:
    result = send_request(base_url, api_key, model, f'[{worker_id}] {prompt}', max_tokens)
    results.append(result)
    if time.monotonic() >= deadline:
      break
    sleep_seconds = random.uniform(min_interval_seconds, max_interval_seconds)
    remaining = deadline - time.monotonic()
    time.sleep(max(0.0, min(sleep_seconds, remaining)))

  return results


def main() -> int:
  parser = argparse.ArgumentParser(description='Load test LiteLLM with parallel clients.')
  parser.add_argument('--base-url', default=DEFAULT_BASE_URL)
  parser.add_argument('--api-key', default=DEFAULT_API_KEY)
  parser.add_argument('--model', default=DEFAULT_MODEL)
  parser.add_argument('--workers', type=int, default=3)
  parser.add_argument('--duration-seconds', type=float, default=120.0)
  parser.add_argument('--requests-per-worker', type=int, default=1)
  parser.add_argument('--min-interval-seconds', type=float, default=5.0)
  parser.add_argument('--max-interval-seconds', type=float, default=10.0)
  parser.add_argument('--max-tokens', type=int, default=32)
  parser.add_argument(
      '--prompt',
      default='Explain a C++ code search service in one short paragraph.',
  )
  args = parser.parse_args()

  wait_for_readiness(f'{args.base_url.rstrip("/")}/health/readiness')

  all_results: list[RequestResult] = []
  lock = threading.Lock()

  def run_worker(worker_id: int) -> None:
    results = worker_loop(
        worker_id,
        args.base_url,
        args.api_key,
        args.model,
        args.prompt,
        args.duration_seconds,
        args.min_interval_seconds,
        args.max_interval_seconds,
        args.requests_per_worker,
        args.max_tokens,
    )
    with lock:
      all_results.extend(results)

  started = time.perf_counter()
  with ThreadPoolExecutor(max_workers=args.workers) as executor:
    futures = [executor.submit(run_worker, worker_id) for worker_id in range(args.workers)]
    for future in as_completed(futures):
      future.result()
  elapsed_seconds = time.perf_counter() - started

  latencies = [result.latency_ms for result in all_results if result.ok]
  errors = [result for result in all_results if not result.ok]

  print(f'requests={len(all_results)} ok={len(latencies)} errors={len(errors)} elapsed_s={elapsed_seconds:.1f}')
  if latencies:
    print(f'p50_ms={percentile(latencies, 0.50):.1f}')
    print(f'p95_ms={percentile(latencies, 0.95):.1f}')
    print(f'p99_ms={percentile(latencies, 0.99):.1f}')
  else:
    print('p50_ms=0.0')
    print('p95_ms=0.0')
    print('p99_ms=0.0')

  if errors:
    first_error = errors[0]
    print(f'first_error_status={first_error.status}')
    print(f'first_error={first_error.error}')

  if errors:
    return 1

  return 0


if __name__ == '__main__':
  raise SystemExit(main())
