import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { z } from 'zod';
import type {
  AdapterConfig,
  AdapterAuthConfig,
  AdapterCoreConfig,
  AdapterDefaultsConfig,
  AdapterServerConfig
} from './types.js';

const serverSchema = z.object({
  host: z.string().min(1).default('127.0.0.1'),
  port: z.coerce.number().int().positive().default(3333),
  path: z.string().min(1).default('/mcp'),
  messagesPath: z.string().min(1).default('/messages'),
  healthPath: z.string().min(1).default('/healthz')
});

const authSchema = z.object({
  apiKeys: z.array(z.string().min(1)).default(['sk-qodeloc-dev'])
});

const coreSchema = z.object({
  baseUrl: z.string().url().default('http://127.0.0.1:3100'),
  timeoutMs: z.coerce.number().int().positive().default(30000),
  modelHeader: z.string().min(1).default('X-QodeLoc-Model')
});

const defaultsSchema = z.object({
  defaultModel: z.string().min(1).default('balanced'),
  workspaceRoot: z.string().default(''),
  maxLocalFiles: z.coerce.number().int().nonnegative().default(8),
  maxChangedFiles: z.coerce.number().int().nonnegative().default(128)
});

const fileSchema = z.object({
  server: serverSchema,
  auth: authSchema,
  core: coreSchema,
  defaults: defaultsSchema
});

function parseJson(content: string, source: string): unknown {
  try {
    return JSON.parse(content);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    throw new Error(`Failed to parse MCP config ${source}: ${message}`);
  }
}

function defaultConfig(): AdapterConfig {
  const workspaceRoot = inferWorkspaceRoot(process.cwd());
  return {
    server: {
      host: '127.0.0.1',
      port: 3333,
      path: '/mcp',
      messagesPath: '/messages',
      healthPath: '/healthz'
    },
    auth: {
      apiKeys: ['sk-qodeloc-dev']
    },
    core: {
      baseUrl: 'http://127.0.0.1:3100',
      timeoutMs: 30000,
      modelHeader: 'X-QodeLoc-Model'
    },
    defaults: {
      defaultModel: 'balanced',
      workspaceRoot,
      maxLocalFiles: 8,
      maxChangedFiles: 128
    },
    configPath: '',
    workspaceRoot
  };
}

function normalizeWorkspaceRoot(value: string, fallback: string): string {
  const trimmed = value.trim();
  if (!trimmed) {
    return fallback;
  }

  const resolved = path.isAbsolute(trimmed) ? trimmed : path.resolve(fallback, trimmed);
  return path.normalize(resolved);
}

function inferWorkspaceRoot(start: string): string {
  let current = path.resolve(start);
  while (true) {
    if (existsSync(path.join(current, 'VERSION'))) {
      return current;
    }

    const parent = path.dirname(current);
    if (parent === current) {
      return start;
    }
    current = path.dirname(current);
  }
}

async function readConfigFile(configPath: string): Promise<unknown> {
  return fs.readFile(configPath, 'utf8').then((content) => parseJson(content, configPath));
}

async function findConfigPath(configPathInput: string): Promise<string> {
  const candidates = [
    configPathInput,
    path.resolve(process.cwd(), 'config.json'),
    path.resolve(process.cwd(), 'mcp-adapter', 'config.json'),
    path.resolve(process.cwd(), 'qodeloc', 'mcp-adapter', 'config.json')
  ];

  for (const candidate of candidates) {
    try {
      await fs.access(candidate);
      return path.resolve(candidate);
    } catch {
      // Ignore missing candidates.
    }
  }

  return path.resolve(configPathInput);
}

function mergeEnv(config: AdapterConfig, env: NodeJS.ProcessEnv): AdapterConfig {
  const merged = structuredClone(config);

  const server = merged.server;
  if (env.QODELOC_MCP_HOST) {
    server.host = env.QODELOC_MCP_HOST;
  }
  if (env.QODELOC_MCP_PORT) {
    server.port = Number(env.QODELOC_MCP_PORT);
  }
  if (env.QODELOC_MCP_PATH) {
    server.path = env.QODELOC_MCP_PATH;
  }
  if (env.QODELOC_MCP_MESSAGES_PATH) {
    server.messagesPath = env.QODELOC_MCP_MESSAGES_PATH;
  }
  if (env.QODELOC_MCP_HEALTH_PATH) {
    server.healthPath = env.QODELOC_MCP_HEALTH_PATH;
  }

  const auth = merged.auth;
  if (env.QODELOC_MCP_API_KEYS) {
    auth.apiKeys = env.QODELOC_MCP_API_KEYS.split(',').map((value) => value.trim()).filter(Boolean);
  }

  const core = merged.core;
  if (env.QODELOC_CORE_API_URL) {
    core.baseUrl = env.QODELOC_CORE_API_URL;
  }
  if (env.QODELOC_CORE_API_TIMEOUT_MS) {
    core.timeoutMs = Number(env.QODELOC_CORE_API_TIMEOUT_MS);
  }
  if (env.QODELOC_CORE_MODEL_HEADER) {
    core.modelHeader = env.QODELOC_CORE_MODEL_HEADER;
  }

  const defaults = merged.defaults;
  if (env.QODELOC_MCP_DEFAULT_MODEL) {
    defaults.defaultModel = env.QODELOC_MCP_DEFAULT_MODEL;
  }
  if (env.QODELOC_MCP_WORKSPACE_ROOT) {
    defaults.workspaceRoot = env.QODELOC_MCP_WORKSPACE_ROOT;
  }
  if (env.QODELOC_MCP_MAX_LOCAL_FILES) {
    defaults.maxLocalFiles = Number(env.QODELOC_MCP_MAX_LOCAL_FILES);
  }
  if (env.QODELOC_MCP_MAX_CHANGED_FILES) {
    defaults.maxChangedFiles = Number(env.QODELOC_MCP_MAX_CHANGED_FILES);
  }

  return merged;
}

export async function loadAdapterConfig(
  configPathInput = process.env.QODELOC_MCP_CONFIG ?? path.resolve(process.cwd(), 'config.json')
): Promise<AdapterConfig> {
  const configPath = await findConfigPath(configPathInput);
  const baseConfig = defaultConfig();

  let loaded = baseConfig;
  try {
    const parsed = await readConfigFile(configPath);
    const validated = fileSchema.parse(parsed);
    loaded = {
      server: validated.server,
      auth: validated.auth,
      core: validated.core,
      defaults: validated.defaults,
      configPath: '',
      workspaceRoot: ''
    };
  } catch (error) {
    if (error instanceof Error && error.message.startsWith('Failed to parse MCP config')) {
      throw error;
    }
    if (error instanceof z.ZodError) {
      throw new Error(`Invalid MCP config ${configPath}: ${error.message}`);
    }
  }

  const merged = mergeEnv(loaded, process.env);
  const parsed = fileSchema.parse(merged);
  const workspaceRoot = normalizeWorkspaceRoot(
    parsed.defaults.workspaceRoot,
    inferWorkspaceRoot(path.dirname(configPath))
  );

  return {
    server: parsed.server as AdapterServerConfig,
    auth: parsed.auth as AdapterAuthConfig,
    core: parsed.core as AdapterCoreConfig,
    defaults: parsed.defaults as AdapterDefaultsConfig,
    configPath,
    workspaceRoot
  };
}

export function normalizeWorkspacePath(
  workspaceRoot: string,
  inputPath: string,
  allowOutsideRoot = false
): string {
  const trimmed = inputPath.trim();
  if (!trimmed) {
    return '';
  }

  const absolutePath = path.isAbsolute(trimmed)
    ? path.normalize(trimmed)
    : path.resolve(workspaceRoot, trimmed);
  const relativePath = path.relative(workspaceRoot, absolutePath);
  if (!allowOutsideRoot && (relativePath.startsWith('..') || path.isAbsolute(relativePath))) {
    throw new Error(`Path escapes workspace root: ${inputPath}`);
  }

  return relativePath || path.basename(absolutePath);
}
