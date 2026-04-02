import assert from 'node:assert/strict';
import { createServer, type IncomingMessage, type Server } from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs/promises';
import test from 'node:test';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { SSEClientTransport } from '@modelcontextprotocol/sdk/client/sse.js';
import type { EventSourceInit } from 'eventsource';
import { createAdapter } from '../src/server.js';
import type { AdapterConfig } from '../src/types.js';

type JsonObject = Record<string, unknown>;

type SearchResult = {
  query: string;
  symbols: Array<{
    symbol: {
      qualified_name: string;
    };
  }>;
};

type ExplainResult = {
  name: string;
  model: string;
  prompt: {
    template_name: string;
  };
  retrieval: {
    symbols: Array<{
      symbol: {
        qualified_name: string;
      };
    }>;
  };
};

type DepsResult = {
  name: string;
  callers: unknown[];
  module_dependencies: unknown[];
};

type CallersResult = {
  name: string;
  callers: Array<{
    qualified_name: string;
  }>;
};

type ModuleOverviewResult = {
  module_name: string;
  module: {
    module_name: string;
  };
  symbols: Array<{
    qualified_name: string;
  }>;
};

type ReindexResult = {
  changed_files: string[];
  local_files: Array<{
    path: string;
    content: string;
  }>;
  core: {
    changed_files: string[];
  };
};

type ToolTextResult = {
  content?: Array<{
    type?: string;
    text?: string;
  }>;
};

interface RecordedRequest {
  method: string;
  path: string;
  headers: Record<string, string | string[] | undefined>;
  body: JsonObject;
}

async function findFreePort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve) => {
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  const port = typeof address === 'object' && address !== null ? address.port : 0;
  await new Promise<void>((resolve) => {
    server.close(() => resolve());
  });
  if (!port) {
    throw new Error('Failed to allocate a free port');
  }
  return port;
}

async function readJson(req: IncomingMessage): Promise<JsonObject> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }

  const raw = Buffer.concat(chunks).toString('utf8');
  return raw.trim() ? (JSON.parse(raw) as JsonObject) : {};
}

async function startFakeCoreServer(port: number): Promise<{ server: Server; requests: RecordedRequest[] }> {
  const requests: RecordedRequest[] = [];

  const server = createServer(async (req, res) => {
    const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
    const body = req.method === 'GET' ? {} : await readJson(req);
    requests.push({
      method: req.method ?? 'GET',
      path: url.pathname,
      headers: req.headers as Record<string, string | string[] | undefined>,
      body
    });

    const model = typeof req.headers['x-qodeloc-model'] === 'string' ? req.headers['x-qodeloc-model'] : '';

    const responseByPath: Record<string, JsonObject> = {
      '/status': {
        running: true,
        host: '127.0.0.1',
        port,
        root_directory: '/tmp/catch2',
        symbol_count: 42,
        module_count: 7,
        indexed_files: 5,
        last_indexed_at_ms: 0,
        last_operation: 'index',
        last_stats: {
          files_scanned: 5,
          files_indexed: 5,
          symbols_indexed: 42,
          parse_errors: 0,
          embedding_batches: 1,
          elapsed_ms: 1
        },
        retriever_ready: true,
        llm_ready: true
      },
      '/search': {
        query: body.query ?? '',
        modules: [
          {
            module_name: 'src',
            module_path: 'src',
            summary: 'Catch2 src',
            public_symbol_count: 2,
            header_count: 1,
            score: 0.98
          }
        ],
        symbols: [
          {
            symbol: {
              symbol_id: 1,
              file_path: 'src/catch_session.cpp',
              module_name: 'src',
              module_path: 'src',
              kind: 'Class',
              qualified_name: 'Catch::Session',
              signature: 'class Session',
              start_line: 10,
              end_line: 30,
              source_text: 'class Session {}'
            },
            score: 0.99,
            context: 'Session context',
            token_count: 3,
            callers: [],
            callees: []
          }
        ]
      },
      '/explain': {
        name: body.name ?? '',
        model,
        symbol: {
          symbol_id: 2,
          file_path: 'src/catch_config.cpp',
          module_name: 'src',
          module_path: 'src',
          kind: 'Class',
          qualified_name: body.name ?? 'Catch::Config',
          signature: 'class Config',
          start_line: 5,
          end_line: 17,
          source_text: 'class Config {}'
        },
        retrieval: {
          modules: [
            {
              module_name: 'src',
              module_path: 'src',
              summary: 'Catch2 src',
              public_symbol_count: 2,
              header_count: 1,
              score: 1
            }
          ],
          symbols: [
            {
              symbol: {
                symbol_id: 2,
                file_path: 'src/catch_config.cpp',
                module_name: 'src',
                module_path: 'src',
                kind: 'Class',
                qualified_name: body.name ?? 'Catch::Config',
                signature: 'class Config',
                start_line: 5,
                end_line: 17,
                source_text: 'class Config {}'
              },
              score: 1,
              context: 'Config context',
              token_count: 4,
              callers: [],
              callees: []
            }
          ]
        },
        prompt: {
          template_name: 'explain',
          context_token_limit: 1024,
          token_count: 64,
          system_text: 'system',
          user_text: 'user',
          messages: [
            { role: 'system', content: 'system' },
            { role: 'user', content: 'user' }
          ]
        },
        completion: {
          content: 'explanation',
          raw: {
            choices: [
              {
                message: {
                  content: 'explanation'
                }
              }
            ]
          }
        }
      },
      '/deps': {
        name: body.name ?? '',
        symbol: {
          symbol_id: 3,
          file_path: 'src/catch_run_context.cpp',
          module_name: 'src',
          module_path: 'src',
          kind: 'Class',
          qualified_name: body.name ?? 'Catch::RunContext',
          signature: 'class RunContext',
          start_line: 15,
          end_line: 55,
          source_text: 'class RunContext {}'
        },
        callers: [
          {
            file_path: 'src/catch_session.cpp',
            module_name: 'src',
            module_path: 'src',
            kind: 'Function',
            qualified_name: 'Catch::Session::run',
            signature: 'void run()',
            start_line: 100,
            end_line: 140
          }
        ],
        callees: [
          {
            file_path: 'src/catch_config.cpp',
            module_name: 'src',
            module_path: 'src',
            kind: 'Class',
            qualified_name: 'Catch::Config',
            signature: 'class Config',
            start_line: 5,
            end_line: 17
          }
        ],
        module_dependencies: [
          { module_name: 'src', module_path: 'src', depth: Number(body.depth ?? 2) }
        ]
      },
      '/callers': {
        name: body.name ?? '',
        symbol: {
          symbol_id: 4,
          file_path: 'src/catch_assertion_result.cpp',
          module_name: 'src',
          module_path: 'src',
          kind: 'Class',
          qualified_name: body.name ?? 'Catch::AssertionResult',
          signature: 'class AssertionResult',
          start_line: 4,
          end_line: 25,
          source_text: 'class AssertionResult {}'
        },
        callers: [
          {
            file_path: 'src/catch_result_builder.cpp',
            module_name: 'src',
            module_path: 'src',
            kind: 'Function',
            qualified_name: 'Catch::ResultBuilder::build',
            signature: 'void build()',
            start_line: 10,
            end_line: 44
          }
        ]
      },
      '/module': {
        module_name: body.module_name ?? 'src',
        module: {
          module_name: 'src',
          module_path: 'src',
          summary: 'Catch2 src',
          public_symbol_count: 2,
          header_count: 1
        },
        symbols: [
          {
            symbol_id: 5,
            file_path: 'src/catch_session.cpp',
            module_name: 'src',
            module_path: 'src',
            kind: 'Class',
            qualified_name: 'Catch::Session',
            signature: 'class Session',
            start_line: 10,
            end_line: 30,
            source_text: 'class Session {}'
          }
        ],
        dependencies: [
          { module_name: 'src', module_path: 'src', depth: Number(body.depth ?? 1) }
        ]
      },
      '/reindex': {
        mode: 'changed_files',
        changed_files: Array.isArray(body.changed_files) ? body.changed_files : [],
        stats: {
          files_scanned: Array.isArray(body.changed_files) ? body.changed_files.length : 0,
          files_indexed: Array.isArray(body.changed_files) ? body.changed_files.length : 0,
          symbols_indexed: 0,
          parse_errors: 0,
          embedding_batches: 0,
          elapsed_ms: 1
        },
        status: {
          running: true,
          host: '127.0.0.1',
          port,
          root_directory: '/tmp/catch2',
          symbol_count: 42,
          module_count: 7,
          indexed_files: 5,
          last_indexed_at_ms: 0,
          last_operation: 'update',
          last_stats: {
            files_scanned: 0,
            files_indexed: 0,
            symbols_indexed: 0,
            parse_errors: 0,
            embedding_batches: 0,
            elapsed_ms: 0
          },
          retriever_ready: true,
          llm_ready: true
        },
        warnings: []
      }
    };

    const response = responseByPath[url.pathname];
    if (!response) {
      res.writeHead(404, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: `Unknown path: ${url.pathname}` }));
      return;
    }

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(response));
  });

  await new Promise<void>((resolve) => {
    server.listen(port, '127.0.0.1', resolve);
  });

  return { server, requests };
}

async function startAdapter(config: AdapterConfig) {
  const adapter = createAdapter(config);
  await adapter.start();
  return adapter;
}

async function callToolJson(
  client: Client,
  name: string,
  arguments_: Record<string, unknown>
): Promise<unknown> {
  const result = await client.callTool({ name, arguments: arguments_ });
  const first = (result as ToolTextResult).content?.[0];
  if (!first || first.type !== 'text' || typeof first.text !== 'string') {
    return {};
  }

  return JSON.parse(first.text) as unknown;
}

test('mcp adapter serves multiple authenticated clients and routes tools', async () => {
  const workspaceRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'qodeloc-mcp-workspace-'));
  const corePort = await findFreePort();
  const mcpPort = await findFreePort();
  const core = await startFakeCoreServer(corePort);

  const config: AdapterConfig = {
    server: {
      host: '127.0.0.1',
      port: mcpPort,
      path: '/mcp',
      messagesPath: '/messages',
      healthPath: '/healthz'
    },
    auth: {
      apiKeys: ['sk-qodeloc-dev', 'sk-alt']
    },
    core: {
      baseUrl: `http://127.0.0.1:${corePort}`,
      timeoutMs: 5000,
      modelHeader: 'X-QodeLoc-Model'
    },
    defaults: {
      defaultModel: 'balanced',
      workspaceRoot,
      maxLocalFiles: 4,
      maxChangedFiles: 16
    },
    configPath: path.join(workspaceRoot, 'config.json'),
    workspaceRoot
  };

  const adapter = await startAdapter(config);

  try {
    const unauthorized = await fetch(`http://127.0.0.1:${mcpPort}/mcp`, {
      headers: {
        Accept: 'text/event-stream'
      }
    });
    assert.equal(unauthorized.status, 401);

    const makeClient = (apiKey: string) => {
      const eventSourceInit = {
        headers: {
          Authorization: `Bearer ${apiKey}`
        }
      } as unknown as EventSourceInit;

      const transport = new SSEClientTransport(new URL(`http://127.0.0.1:${mcpPort}/mcp`), {
        eventSourceInit,
        requestInit: {
          headers: {
            Authorization: `Bearer ${apiKey}`
          }
        }
      });

      const client = new Client(
        { name: 'qodeloc-mcp-test', version: '0.1.0' },
        {
          capabilities: {}
        }
      );

      return { client, transport };
    };

    const first = makeClient('sk-qodeloc-dev');
    const second = makeClient('sk-alt');

    await Promise.all([first.client.connect(first.transport), second.client.connect(second.transport)]);

    const [firstTools, secondTools] = await Promise.all([
      first.client.listTools(),
      second.client.listTools()
    ]);
    const toolNames = new Set(firstTools.tools.map((tool) => tool.name));
    assert.ok(toolNames.has('search_codebase'));
    assert.ok(toolNames.has('get_symbol'));
    assert.ok(toolNames.has('get_deps'));
    assert.ok(toolNames.has('explain_symbol'));
    assert.ok(toolNames.has('find_callers'));
    assert.ok(toolNames.has('get_module_overview'));
    assert.ok(toolNames.has('sync_workspace_delta'));
    assert.equal(secondTools.tools.length, firstTools.tools.length);

    const health = await fetch(`http://127.0.0.1:${mcpPort}/healthz`);
    const healthPayload = (await health.json()) as JsonObject;
    assert.equal(healthPayload.sessions, 2);

    const search = (await callToolJson(first.client, 'search_codebase', {
      query: 'Where is Catch::Session defined?',
      model: 'balanced'
    })) as SearchResult;
    assert.equal(search.query, 'Where is Catch::Session defined?');
    assert.ok(search.symbols.length > 0);
    assert.equal(search.symbols[0]!.symbol.qualified_name, 'Catch::Session');

    const explain = (await callToolJson(first.client, 'explain_symbol', {
      name: 'Catch::Config',
      model: 'deep',
      local_files: [
        {
          path: path.join(workspaceRoot, 'src/local.cpp'),
          content: 'int local() { return 42; }'
        }
      ]
    })) as ExplainResult;
    assert.equal(explain.name, 'Catch::Config');
    assert.equal(explain.model, 'deep');
    assert.equal(explain.prompt.template_name, 'explain');
    assert.ok(explain.retrieval.symbols.length > 0);
    assert.equal(explain.retrieval.symbols[0]!.symbol.qualified_name, 'Catch::Config');

    const deps = (await callToolJson(first.client, 'get_deps', {
      name: 'Catch::RunContext',
      depth: 3
    })) as DepsResult;
    assert.equal(deps.name, 'Catch::RunContext');
    assert.ok(Array.isArray(deps.callers));
    assert.ok(Array.isArray(deps.module_dependencies));

    const callers = (await callToolJson(first.client, 'find_callers', {
      name: 'Catch::AssertionResult'
    })) as CallersResult;
    assert.equal(callers.name, 'Catch::AssertionResult');
    assert.ok(callers.callers.length > 0);
    assert.equal(callers.callers[0]!.qualified_name, 'Catch::ResultBuilder::build');

    const moduleOverview = (await callToolJson(first.client, 'get_module_overview', {
      module_name: 'src'
    })) as ModuleOverviewResult;
    assert.equal(moduleOverview.module_name, 'src');
    assert.equal(moduleOverview.module.module_name, 'src');
    assert.ok(moduleOverview.symbols.length > 0);
    assert.equal(moduleOverview.symbols[0]!.qualified_name, 'Catch::Session');

    const reindex = (await callToolJson(first.client, 'sync_workspace_delta', {
      changed_files: [path.join(workspaceRoot, 'src/changed.cpp')],
      local_files: [
        {
          path: path.join(workspaceRoot, 'include/changed.hpp'),
          content: '#pragma once'
        }
      ]
    })) as ReindexResult;
    assert.deepEqual(reindex.changed_files, ['src/changed.cpp']);
    assert.deepEqual(reindex.local_files, [
      {
        path: 'include/changed.hpp',
        content: '#pragma once'
      }
    ]);
    assert.deepEqual(reindex.core.changed_files, ['src/changed.cpp']);

    const explainHeaders = core.requests.find((request) => request.path === '/explain');
    assert.ok(explainHeaders);
    assert.equal(explainHeaders?.headers['x-qodeloc-model'], 'deep');
    assert.deepEqual(explainHeaders?.body.local_files, [
      {
        path: 'src/local.cpp',
        content: 'int local() { return 42; }'
      }
    ]);

    const reindexRequest = core.requests.find((request) => request.path === '/reindex');
    assert.ok(reindexRequest);
    assert.deepEqual(reindexRequest?.body.changed_files, ['src/changed.cpp']);

    await Promise.all([first.transport.close(), second.transport.close()]);
  } finally {
    await adapter.stop();
    await new Promise<void>((resolve) => {
      core.server.close(() => resolve());
    });
    await fs.rm(workspaceRoot, { recursive: true, force: true });
  }
});
