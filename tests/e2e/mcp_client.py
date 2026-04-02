from __future__ import annotations

import json
import queue
import threading
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any


def _iter_sse_events(stream):
    event_name: str | None = None
    data_lines: list[str] = []

    while True:
      raw = stream.readline()
      if not raw:
        if event_name is not None or data_lines:
          yield event_name, "\n".join(data_lines)
        return

      line = raw.decode('utf-8').rstrip('\r\n')
      if not line:
        if event_name is not None or data_lines:
          yield event_name, "\n".join(data_lines)
        event_name = None
        data_lines = []
        continue

      if line.startswith('event:'):
        event_name = line[len('event:'):].strip()
        continue

      if line.startswith('data:'):
        data_lines.append(line[len('data:'):].lstrip())


@dataclass(slots=True)
class ToolCallResult:
  raw: dict[str, Any]
  parsed: dict[str, Any]


class SseMcpClient:
  def __init__(self, base_url: str, api_key: str, timeout_seconds: float = 30.0):
    self.base_url = base_url.rstrip('/')
    self.api_key = api_key
    self.timeout_seconds = timeout_seconds
    self.session_id: str | None = None
    self.messages_url: str | None = None
    self._stream = None
    self._reader_thread: threading.Thread | None = None
    self._pending: dict[int, queue.Queue[dict[str, Any]]] = {}
    self._pending_lock = threading.Lock()
    self._next_id = 1
    self._closed = False

  def connect(self) -> None:
    request = urllib.request.Request(
      f"{self.base_url}/mcp",
      headers={
        'Authorization': f'Bearer {self.api_key}',
        'Accept': 'text/event-stream'
      },
      method='GET'
    )
    self._stream = urllib.request.urlopen(request, timeout=self.timeout_seconds)

    endpoint_path = None
    for event_name, data in _iter_sse_events(self._stream):
      if event_name == 'endpoint':
        endpoint_path = data.strip()
        break

    if endpoint_path is None:
      raise RuntimeError('MCP server did not emit an endpoint event')

    self.messages_url = urllib.parse.urljoin(f"{self.base_url}/", endpoint_path.lstrip('/'))
    parsed = urllib.parse.urlparse(self.messages_url)
    query = urllib.parse.parse_qs(parsed.query)
    session_ids = query.get('sessionId') or []
    if not session_ids:
      raise RuntimeError(f'Could not extract sessionId from endpoint: {endpoint_path}')
    self.session_id = session_ids[0]

    self._reader_thread = threading.Thread(target=self._read_events, name='mcp-sse-reader', daemon=True)
    self._reader_thread.start()
    self.request(
      'initialize',
      {
        'protocolVersion': '2024-11-05',
        'capabilities': {},
        'clientInfo': {
          'name': 'qodeloc-e2e',
          'version': '0.1.0'
        }
      },
      expect_result=True
    )
    self.notify('notifications/initialized', {})

  def close(self) -> None:
    self._closed = True
    if self._stream is not None:
      try:
        self._stream.close()
      except Exception:
        pass
    if self._reader_thread is not None and self._reader_thread.is_alive():
      self._reader_thread.join(timeout=2.0)

  def notify(self, method: str, params: dict[str, Any]) -> None:
    self._post_json(
      {
        'jsonrpc': '2.0',
        'method': method,
        'params': params
      }
    )

  def request(self, method: str, params: dict[str, Any], expect_result: bool = True) -> dict[str, Any]:
    request_id = self._next_request_id()
    response_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
    with self._pending_lock:
      self._pending[request_id] = response_queue

    try:
      self._post_json(
        {
          'jsonrpc': '2.0',
          'id': request_id,
          'method': method,
          'params': params
        }
      )
      if not expect_result:
        return {}

      message = response_queue.get(timeout=self.timeout_seconds)
      if 'error' in message:
        raise RuntimeError(json.dumps(message['error'], indent=2))
      return message.get('result', {})
    finally:
      with self._pending_lock:
        self._pending.pop(request_id, None)

  def call_tool(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    result = self.request(
      'tools/call',
      {
        'name': name,
        'arguments': arguments
      }
    )
    return result

  def call_tool_json(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
    result = self.call_tool(name, arguments)
    content = result.get('content', [])
    if not content:
      return result

    first = content[0]
    if isinstance(first, dict) and first.get('type') == 'text' and isinstance(first.get('text'), str):
      text = first['text']
      try:
        return json.loads(text)
      except json.JSONDecodeError:
        return {'text': text, '_raw': result}

    return result

  def _next_request_id(self) -> int:
    request_id = self._next_id
    self._next_id += 1
    return request_id

  def _post_json(self, payload: dict[str, Any]) -> None:
    if self.messages_url is None:
      raise RuntimeError('MCP client is not connected')

    request = urllib.request.Request(
      self.messages_url,
      data=json.dumps(payload).encode('utf-8'),
      headers={
        'Authorization': f'Bearer {self.api_key}',
        'Content-Type': 'application/json'
      },
      method='POST'
    )
    with urllib.request.urlopen(request, timeout=self.timeout_seconds) as response:
      # The SSE transport acknowledges accepted requests with HTTP 202.
      response.read()

  def _read_events(self) -> None:
    if self._stream is None:
      return

    try:
      for event_name, data in _iter_sse_events(self._stream):
        if self._closed:
          return
        if event_name != 'message' or not data:
          continue

        try:
          message = json.loads(data)
        except json.JSONDecodeError:
          continue

        request_id = message.get('id')
        if request_id is None:
          continue

        with self._pending_lock:
          response_queue = self._pending.get(int(request_id))

        if response_queue is None:
          continue

        try:
          response_queue.put_nowait(message)
        except queue.Full:
          pass
    except Exception:
      if not self._closed:
        raise
