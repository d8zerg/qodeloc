import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { loadAdapterConfig, normalizeWorkspacePath } from '../src/config.js';

test('normalizeWorkspacePath keeps paths inside the workspace', () => {
  const workspaceRoot = path.resolve('/tmp/workspace');
  assert.equal(normalizeWorkspacePath(workspaceRoot, 'src/main.cpp'), 'src/main.cpp');
  assert.equal(
    normalizeWorkspacePath(workspaceRoot, path.join(workspaceRoot, 'src/main.cpp')),
    'src/main.cpp'
  );
});

test('loadAdapterConfig applies env overrides', async () => {
  const tmpDir = await fs.mkdtemp(path.join(os.tmpdir(), 'qodeloc-mcp-config-'));
  const configPath = path.join(tmpDir, 'config.json');
  await fs.writeFile(
    configPath,
    JSON.stringify(
      {
        server: { host: '0.0.0.0', port: 4999, path: '/mcp', messagesPath: '/messages', healthPath: '/healthz' },
        auth: { apiKeys: ['alpha'] },
        core: { baseUrl: 'http://127.0.0.1:3100', timeoutMs: 1000, modelHeader: 'X-QodeLoc-Model' },
        defaults: { defaultModel: 'balanced', workspaceRoot: '', maxLocalFiles: 4, maxChangedFiles: 16 }
      },
      null,
      2
    ),
    'utf8'
  );

  const previousConfig = process.env.QODELOC_MCP_CONFIG;
  const previousApiKeys = process.env.QODELOC_MCP_API_KEYS;
  const previousPort = process.env.QODELOC_MCP_PORT;
  process.env.QODELOC_MCP_CONFIG = configPath;
  process.env.QODELOC_MCP_API_KEYS = 'beta,gamma';
  process.env.QODELOC_MCP_PORT = '5001';

  try {
    const config = await loadAdapterConfig();
    assert.equal(config.server.port, 5001);
    assert.deepEqual(config.auth.apiKeys, ['beta', 'gamma']);
    assert.equal(config.defaults.defaultModel, 'balanced');
  } finally {
    if (previousConfig === undefined) {
      delete process.env.QODELOC_MCP_CONFIG;
    } else {
      process.env.QODELOC_MCP_CONFIG = previousConfig;
    }
    if (previousApiKeys === undefined) {
      delete process.env.QODELOC_MCP_API_KEYS;
    } else {
      process.env.QODELOC_MCP_API_KEYS = previousApiKeys;
    }
    if (previousPort === undefined) {
      delete process.env.QODELOC_MCP_PORT;
    } else {
      process.env.QODELOC_MCP_PORT = previousPort;
    }
  }
});
