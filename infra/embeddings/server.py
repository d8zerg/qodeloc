from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Iterable

import torch
from transformers import AutoModel, AutoTokenizer


@dataclass(slots=True)
class EmbeddingRuntime:
  model_dir: Path
  device: str = 'auto'
  max_length: int = 512

  def __post_init__(self) -> None:
    resolved_device = self.device
    if resolved_device == 'auto':
      resolved_device = 'cuda' if torch.cuda.is_available() else 'cpu'
    self.device = resolved_device

    self.tokenizer = AutoTokenizer.from_pretrained(self.model_dir, trust_remote_code=True)
    self.model = AutoModel.from_pretrained(self.model_dir, trust_remote_code=True)
    self.model.to(self.device)
    self.model.eval()
    self.dimension = int(getattr(self.model.config, 'hidden_size', 0) or 0)

  def embed(self, texts: Iterable[str]) -> list[list[float]]:
    normalized = [str(text) for text in texts]
    if not normalized:
      return []

    encoded = self.tokenizer(
        normalized,
        padding=True,
        truncation=True,
        max_length=self.max_length,
        return_tensors='pt',
    )
    encoded = {key: value.to(self.device) for key, value in encoded.items()}

    with torch.inference_mode():
      outputs = self.model(**encoded)
      if hasattr(outputs, 'sentence_embedding') and outputs.sentence_embedding is not None:
        embeddings = outputs.sentence_embedding
      elif hasattr(outputs, 'pooler_output') and outputs.pooler_output is not None:
        embeddings = outputs.pooler_output
      else:
        last_hidden_state = outputs.last_hidden_state
        attention_mask = encoded['attention_mask'].unsqueeze(-1).expand_as(last_hidden_state).float()
        summed = (last_hidden_state * attention_mask).sum(dim=1)
        counts = attention_mask.sum(dim=1).clamp(min=1.0)
        embeddings = summed / counts

      embeddings = torch.nn.functional.normalize(embeddings, p=2, dim=1)

    return embeddings.detach().cpu().tolist()


def parse_input(payload: dict[str, object]) -> list[str]:
  raw_input = payload.get('input', [])
  if isinstance(raw_input, str):
    return [raw_input]
  if isinstance(raw_input, list):
    return [str(item) for item in raw_input]
  return []


def json_response(handler: BaseHTTPRequestHandler, status: int, payload: dict[str, object]) -> None:
  body = json.dumps(payload).encode('utf-8')
  handler.send_response(status)
  handler.send_header('Content-Type', 'application/json')
  handler.send_header('Content-Length', str(len(body)))
  handler.end_headers()
  handler.wfile.write(body)


def create_handler(runtime: EmbeddingRuntime):
  class Handler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args) -> None:  # noqa: A003
      return

    def do_GET(self) -> None:  # noqa: N802
      if self.path == '/healthz':
        json_response(
            self,
            200,
            {
                'status': 'ok',
                'model_loaded': True,
                'model_dir': str(runtime.model_dir),
                'device': runtime.device,
                'dimension': runtime.dimension,
            },
        )
        return

      json_response(self, 404, {'error': f'Unknown path: {self.path}'})

    def do_POST(self) -> None:  # noqa: N802
      if self.path != '/v1/embeddings':
        json_response(self, 404, {'error': f'Unknown path: {self.path}'})
        return

      length = int(self.headers.get('Content-Length', '0'))
      raw = self.rfile.read(length).decode('utf-8') if length > 0 else '{}'
      payload = json.loads(raw or '{}')
      inputs = parse_input(payload)
      embeddings = runtime.embed(inputs)

      data = []
      for index, embedding in enumerate(embeddings):
        data.append(
            {
                'object': 'embedding',
                'index': index,
                'embedding': embedding,
            }
        )

      json_response(
          self,
          200,
          {
              'object': 'list',
              'model': payload.get('model', 'jina-code'),
              'data': data,
              'usage': {
                  'prompt_tokens': 0,
                  'total_tokens': 0,
                },
          },
      )

  return Handler


def main() -> None:
  parser = argparse.ArgumentParser(description='QodeLoc embedding server')
  parser.add_argument('--host', default=os.getenv('QODELOC_EMBEDDER_HOST', '0.0.0.0'))
  parser.add_argument('--port', type=int, default=int(os.getenv('QODELOC_EMBEDDER_PORT', '8081')))
  parser.add_argument('--model-dir', default=os.getenv('QODELOC_EMBEDDER_MODEL_DIR', '/models/jina-code'))
  parser.add_argument('--device', default=os.getenv('QODELOC_EMBEDDER_DEVICE', 'auto'))
  parser.add_argument('--max-length', type=int, default=int(os.getenv('QODELOC_EMBEDDER_MAX_LENGTH', '512')))
  args = parser.parse_args()

  runtime = EmbeddingRuntime(model_dir=Path(args.model_dir), device=args.device, max_length=args.max_length)
  server = ThreadingHTTPServer((args.host, args.port), create_handler(runtime))
  print(
      f"[qodeloc-embeddings] listening on {args.host}:{args.port} "
      f"model={runtime.model_dir} device={runtime.device} dim={runtime.dimension}",
      flush=True,
  )
  server.serve_forever()


if __name__ == '__main__':
  main()
