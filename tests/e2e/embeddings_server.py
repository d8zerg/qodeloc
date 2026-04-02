from __future__ import annotations

import hashlib
import json
import re
import threading
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Iterable


_TOKEN_RE = re.compile(r"[A-Za-z0-9_]+")


def _tokenize(text: str) -> list[str]:
  return _TOKEN_RE.findall(text.lower())


def _hash_index(token: str, dimension: int, salt: str = '') -> tuple[int, float]:
  digest = hashlib.blake2b(f'{salt}{token}'.encode('utf-8'), digest_size=16).digest()
  index = int.from_bytes(digest[:4], 'big') % dimension
  sign = 1.0 if digest[4] % 2 == 0 else -1.0
  return index, sign


def _vectorize(text: str, dimension: int = 128) -> list[float]:
  vector = [0.0] * dimension
  tokens = _tokenize(text)
  if not tokens:
    return vector

  for token in tokens:
    primary_index, primary_sign = _hash_index(token, dimension)
    vector[primary_index] += primary_sign

    secondary_index, secondary_sign = _hash_index(token, dimension, salt='secondary:')
    vector[secondary_index] += secondary_sign * 0.5

    tertiary_index, tertiary_sign = _hash_index(token, dimension, salt='tertiary:')
    vector[tertiary_index] += tertiary_sign * 0.25

  norm = sum(value * value for value in vector) ** 0.5
  if norm == 0.0:
    return vector

  return [value / norm for value in vector]


@dataclass(slots=True)
class EmbeddingServer:
  port: int
  host: str = '127.0.0.1'
  path: str = '/v1/embeddings'
  _server: ThreadingHTTPServer | None = field(default=None, init=False)
  _thread: threading.Thread | None = field(default=None, init=False)

  def start(self) -> None:
    server = self

    class Handler(BaseHTTPRequestHandler):
      def log_message(self, format: str, *args) -> None:  # noqa: A003
        return

      def _write_json(self, payload: dict[str, object], status: int = 200) -> None:
        body = json.dumps(payload).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

      def do_GET(self) -> None:  # noqa: N802
        if self.path == '/healthz':
          self._write_json({'status': 'ok'})
          return

        self._write_json({'error': f'Unknown path: {self.path}'}, status=404)

      def do_POST(self) -> None:  # noqa: N802
        if self.path != server.path:
          self._write_json({'error': f'Unknown path: {self.path}'}, status=404)
          return

        length = int(self.headers.get('Content-Length', '0'))
        raw = self.rfile.read(length).decode('utf-8') if length > 0 else '{}'
        payload = json.loads(raw or '{}')

        inputs = payload.get('input', [])
        if isinstance(inputs, str):
          normalized_inputs: Iterable[str] = [inputs]
        else:
          normalized_inputs = [str(item) for item in inputs]

        data = []
        for index, text in enumerate(normalized_inputs):
          data.append(
              {
                  'object': 'embedding',
                  'index': index,
                  'embedding': _vectorize(text),
              }
          )

        self._write_json(
            {
                'object': 'list',
                'model': payload.get('model', 'qodeloc-embedding'),
                'data': data,
            }
        )

    self._server = ThreadingHTTPServer((self.host, self.port), Handler)
    self.port = self._server.server_address[1]
    self._thread = threading.Thread(target=self._server.serve_forever, name='embedding-server', daemon=True)
    self._thread.start()

  def stop(self) -> None:
    if self._server is not None:
      self._server.shutdown()
      self._server.server_close()
      self._server = None

    if self._thread is not None and self._thread.is_alive():
      self._thread.join(timeout=5.0)
      self._thread = None
