from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from embeddings_server import EmbeddingServer
from mcp_client import SseMcpClient


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_QUERIES = REPO_ROOT / 'tests/e2e/fixtures/catch2/queries_smoke.json'
DEFAULT_REPO = REPO_ROOT / 'tests/e2e/fixtures/repos/catch2'
DEFAULT_CORE_BIN = REPO_ROOT / 'core/build/debug/qodeloc-core'
DEFAULT_RESULTS_DIR = REPO_ROOT / 'tests/e2e/results'
DEFAULT_CORE_URL = 'http://127.0.0.1:3100'
DEFAULT_MCP_URL = 'http://127.0.0.1:3333'
DEFAULT_LLM_READINESS_URL = 'http://127.0.0.1:4000/health/readiness'


def find_free_port() -> int:
  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.bind(('127.0.0.1', 0))
    return int(sock.getsockname()[1])


def wait_for_http_json(
    url: str,
    *,
    timeout_seconds: float = 180.0,
    predicate: Callable[[dict[str, Any]], bool] | None = None,
    headers: dict[str, str] | None = None
) -> dict[str, Any]:
  started = time.monotonic()
  last_error: str | None = None
  predicate = predicate or (lambda _: True)

  while time.monotonic() - started < timeout_seconds:
    try:
      request = Request(url, headers=headers or {})
      with urlopen(request, timeout=10.0) as response:
        payload = json.loads(response.read().decode('utf-8'))
        if isinstance(payload, dict) and predicate(payload):
          return payload
        last_error = f'Predicate not satisfied for {url}: {payload!r}'
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
      last_error = str(exc)

    time.sleep(1.0)

  raise RuntimeError(f'Timed out waiting for {url}: {last_error or "unknown error"}')


def wait_for_http_ok(url: str, *, timeout_seconds: float = 180.0) -> None:
  wait_for_http_json(url, timeout_seconds=timeout_seconds, predicate=lambda payload: True)


def start_process(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.Popen[str]:
  return subprocess.Popen(
      command,
      cwd=str(cwd),
      env=env,
      start_new_session=True,
  )


def stop_process(process: subprocess.Popen[str] | None) -> None:
  if process is None:
    return

  if process.poll() is not None:
    return

  try:
    os.killpg(process.pid, signal.SIGTERM)
  except ProcessLookupError:
    return

  try:
    process.wait(timeout=15)
  except subprocess.TimeoutExpired:
    try:
      os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
      return
    process.wait(timeout=15)


def load_queries(path: Path) -> dict[str, Any]:
  with path.open('r', encoding='utf-8') as handle:
    return json.load(handle)


def extract_symbol_name(query: str, prefix: str) -> str:
  if query.startswith(prefix):
    value = query[len(prefix):].strip()
    if value.endswith('?'):
      value = value[:-1]
    return value
  return query


def build_tool_arguments(query_entry: dict[str, Any]) -> dict[str, Any]:
  tool = query_entry['tool']
  query = query_entry['query']
  arguments: dict[str, Any] = {}

  if tool == 'search_codebase':
    arguments['query'] = query
  elif tool in {'get_symbol', 'explain_symbol'}:
    arguments['name'] = extract_symbol_name(query, 'Explain ')
  elif tool == 'get_deps':
    arguments['name'] = extract_symbol_name(query, 'Dependencies of ')
    arguments['depth'] = int(query_entry.get('depth', 2))
  elif tool == 'find_callers':
    arguments['name'] = extract_symbol_name(query, 'Who calls ')
  elif tool == 'get_module_overview':
    arguments['module_name'] = query_entry.get('module_name', extract_symbol_name(query, 'Overview of the '))
  elif tool == 'sync_workspace_delta':
    arguments['changed_files'] = query_entry.get('changed_files', [])
  else:
    raise ValueError(f'Unsupported tool: {tool}')

  if 'model' in query_entry:
    arguments['model'] = query_entry['model']

  if tool == 'sync_workspace_delta':
    arguments['local_files'] = query_entry.get('local_files', [])

  return arguments


def extract_ranked_names(tool: str, payload: dict[str, Any]) -> list[str]:
  if tool == 'search_codebase':
    return [
        symbol.get('symbol', {}).get('qualified_name', '')
        for symbol in payload.get('symbols', [])
        if isinstance(symbol, dict)
    ]

  if tool in {'get_symbol', 'explain_symbol'}:
    ranked = []
    if isinstance(payload.get('symbol'), dict):
      ranked.append(str(payload['symbol'].get('qualified_name', '')))
    ranked.extend(
        symbol.get('symbol', {}).get('qualified_name', '')
        for symbol in payload.get('retrieval', {}).get('symbols', [])
        if isinstance(symbol, dict)
    )
    return ranked

  if tool == 'get_deps':
    ranked = []
    if isinstance(payload.get('symbol'), dict):
      ranked.append(str(payload['symbol'].get('qualified_name', '')))
    ranked.extend(
        callee.get('qualified_name', '')
        for callee in payload.get('callees', [])
        if isinstance(callee, dict)
    )
    ranked.extend(
        caller.get('qualified_name', '')
        for caller in payload.get('callers', [])
        if isinstance(caller, dict)
    )
    ranked.extend(
        dependency.get('module_name', '')
        for dependency in payload.get('module_dependencies', [])
        if isinstance(dependency, dict)
    )
    return ranked

  if tool == 'find_callers':
    ranked = []
    if isinstance(payload.get('symbol'), dict):
      ranked.append(str(payload['symbol'].get('qualified_name', '')))
    ranked.extend(
        caller.get('qualified_name', '')
        for caller in payload.get('callers', [])
        if isinstance(caller, dict)
    )
    return ranked

  if tool == 'get_module_overview':
    ranked = []
    module = payload.get('module')
    if isinstance(module, dict):
      ranked.append(str(module.get('module_name', '')))
    ranked.extend(
        symbol.get('qualified_name', '')
        for symbol in payload.get('symbols', [])
        if isinstance(symbol, dict)
    )
    return ranked

  return []


def first_relevant_rank(ranked: list[str], expected: list[str]) -> int | None:
  expected_set = {item for item in expected if item}
  if not expected_set:
    return None

  for index, name in enumerate(ranked, start=1):
    if name in expected_set:
      return index
  return None


def safe_average(values: list[float]) -> float:
  return statistics.mean(values) if values else 0.0


def summarize_metrics(rows: list[dict[str, Any]]) -> dict[str, Any]:
  scored_rows = [row for row in rows if row.get('group') != 'auxiliary']
  by_group: dict[str, list[dict[str, Any]]] = defaultdict(list)
  for row in scored_rows:
    by_group[row['group']].append(row)

  summary: dict[str, Any] = {
      'queries': len(rows),
      'scored_queries': len(scored_rows),
      'groups': {},
      'overall_mrr': safe_average([float(row['mrr']) for row in scored_rows]),
  }

  for group, group_rows in by_group.items():
    precision_scores = [1.0 if row['top5_hit'] else 0.0 for row in group_rows]
    recall_scores = [1.0 if row['top10_hit'] else 0.0 for row in group_rows]
    summary['groups'][group] = {
        'count': len(group_rows),
        'precision@5': safe_average(precision_scores),
        'recall@10': safe_average(recall_scores),
        'mrr': safe_average([float(row['mrr']) for row in group_rows]),
    }

  return summary


def wait_for_core_bootstrap_ready(core_url: str, timeout_seconds: float = 300.0) -> dict[str, Any]:
  def predicate(payload: dict[str, Any]) -> bool:
    state = str(payload.get('bootstrap_state', '')).lower()
    if state == 'error':
      raise RuntimeError(
          f"Core bootstrap failed: {payload.get('bootstrap_message', 'unknown error')}"
      )
    return bool(payload.get('bootstrap_complete')) or state == 'ready'

  return wait_for_http_json(f'{core_url}/status', timeout_seconds=timeout_seconds, predicate=predicate)


def main() -> int:
  parser = argparse.ArgumentParser(description='Run the full QodeLoc MCP e2e baseline.')
  parser.add_argument('--repo', type=Path, default=DEFAULT_REPO, help='Path to the Catch2 checkout.')
  parser.add_argument('--queries', type=Path, default=DEFAULT_QUERIES, help='Benchmark query manifest.')
  parser.add_argument('--core-bin', type=Path, default=DEFAULT_CORE_BIN, help='Core Engine binary.')
  parser.add_argument('--results-dir', type=Path, default=DEFAULT_RESULTS_DIR, help='Directory for results.')
  parser.add_argument('--core-url', default=DEFAULT_CORE_URL, help='Core API base URL.')
  parser.add_argument('--mcp-url', default=DEFAULT_MCP_URL, help='MCP adapter base URL.')
  parser.add_argument('--llm-readiness-url', default=DEFAULT_LLM_READINESS_URL, help='LiteLLM readiness URL.')
  parser.add_argument('--duration-seconds', type=float, default=0.0, help='Reserved for future smoke modes.')
  args = parser.parse_args()

  if not args.repo.exists():
    raise SystemExit(
        f'Missing fixture repo at {args.repo}. Run `make download-e2e-catch2` first.'
    )
  if not args.core_bin.exists():
    raise SystemExit(
        f'Missing Core binary at {args.core_bin}. Run `make build` first.'
    )
  if not args.queries.exists():
    raise SystemExit(f'Missing query manifest at {args.queries}.')

  args.results_dir.mkdir(parents=True, exist_ok=True)
  run_dir = args.results_dir / datetime.now(timezone.utc).strftime('run-%Y%m%dT%H%M%SZ')
  run_dir.mkdir(parents=True, exist_ok=True)

  queries_manifest = load_queries(args.queries)
  query_entries = list(queries_manifest.get('queries', []))
  if not query_entries:
    raise SystemExit(f'No queries found in {args.queries}')

  embedder_port = find_free_port()
  embedding_server = EmbeddingServer(port=embedder_port)
  embedding_server.start()

  env = os.environ.copy()
  env.update(
      {
          'QODELOC_EMBEDDER_HOST': '127.0.0.1',
          'QODELOC_EMBEDDER_PORT': str(embedder_port),
          'QODELOC_EMBEDDER_API_PATH': '/v1/embeddings',
          'QODELOC_EMBEDDER_MODEL': 'qodeloc-embedding',
          'QODELOC_INDEXER_ROOT_DIRECTORY': str(args.repo),
          'QODELOC_STORAGE_DB_PATH': str(run_dir / 'e2e.duckdb'),
          'QODELOC_LLM_MODEL': 'balanced',
          'QODELOC_LLM_MAX_TOKENS': '48',
          'QODELOC_API_PORT': '3100',
          'QODELOC_PROMPT_CONTEXT_TOKEN_LIMIT': '1536',
          'QODELOC_PROMPT_MODULE_LIMIT': '2',
          'QODELOC_PROMPT_SYMBOL_LIMIT': '4',
          'QODELOC_PROMPT_LOCAL_FILE_LIMIT': '1',
          'QODELOC_RETRIEVER_RELATED_SYMBOL_LIMIT': '2',
          'QODELOC_RETRIEVER_CONTEXT_TOKEN_LIMIT': '128',
          'QODELOC_HIERARCHY_MODULE_TOP_K': '2',
          'QODELOC_HIERARCHY_SYMBOL_TOP_K': '3',
      }
  )

  core_process: subprocess.Popen[str] | None = None
  adapter_process: subprocess.Popen[str] | None = None

  try:
    wait_for_http_json(args.llm_readiness_url, timeout_seconds=180.0)
    core_process = start_process([str(args.core_bin)], cwd=REPO_ROOT, env=env)
    wait_for_core_bootstrap_ready(args.core_url, timeout_seconds=300.0)

    adapter_process = start_process(
        ['npm', '--prefix', 'mcp-adapter', 'run', 'dev'],
        cwd=REPO_ROOT,
        env=env,
    )
    wait_for_http_json(
        f'{args.mcp_url}/healthz',
        timeout_seconds=120.0,
        predicate=lambda payload: payload.get('status') == 'ok'
    )

    client = SseMcpClient(args.mcp_url, 'sk-qodeloc-dev', timeout_seconds=60.0)
    client.connect()
    try:
      results: list[dict[str, Any]] = []
      for entry in query_entries:
        tool = entry['tool']
        arguments = build_tool_arguments(entry)
        payload = client.call_tool_json(tool, arguments)
        ranked = extract_ranked_names(tool, payload if isinstance(payload, dict) else {})
        expected = [str(item) for item in entry.get('expected_symbols', [])]
        rank = first_relevant_rank(ranked, expected)
        top5_hit = rank is not None and rank <= 5
        top10_hit = rank is not None and rank <= 10
        mrr = 1.0 / rank if rank else 0.0
        if tool == 'search_codebase':
          group = 'semantic'
        elif tool in {'get_deps', 'find_callers', 'get_module_overview'}:
          group = 'structural'
        else:
          group = 'auxiliary'
        results.append(
            {
                'id': entry.get('id'),
                'tool': tool,
                'expected_symbols': expected,
                'ranked': ranked,
                'first_relevant_rank': rank,
                'top5_hit': top5_hit,
                'top10_hit': top10_hit,
                'mrr': mrr,
                'group': group,
            }
        )
        print(f"[{entry.get('id')}] {tool}: rank={rank or 'miss'} expected={expected}")

      summary = summarize_metrics(results)
      report = {
          'repository': queries_manifest.get('repository', {}),
          'summary': summary,
          'results': results,
      }
      report_path = run_dir / 'report.json'
      report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')

      semantic = summary['groups'].get('semantic', {})
      structural = summary['groups'].get('structural', {})
      semantic_precision = float(semantic.get('precision@5', 0.0))
      structural_recall = float(structural.get('recall@10', 0.0))

      print()
      print('E2E summary')
      print(f"  semantic precision@5: {semantic_precision:.3f}")
      print(f"  structural recall@10: {structural_recall:.3f}")
      print(f"  overall MRR: {float(summary['overall_mrr']):.3f}")
      print(f'  report: {report_path}')

      if semantic_precision < 0.70:
        raise SystemExit(f'Semantic precision@5 below threshold: {semantic_precision:.3f} < 0.700')
      if structural_recall < 0.85:
        raise SystemExit(f'Structural recall@10 below threshold: {structural_recall:.3f} < 0.850')
      return 0
    finally:
      client.close()
  finally:
    stop_process(adapter_process)
    stop_process(core_process)
    embedding_server.stop()


if __name__ == '__main__':
  raise SystemExit(main())
