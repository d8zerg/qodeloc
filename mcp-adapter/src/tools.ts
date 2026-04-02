import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import * as z from 'zod';
import { normalizeWorkspacePath } from './config.js';
import { CoreClient } from './core-client.js';
import type { AdapterConfig, LocalFileInput } from './types.js';

const localFileSchema = z.object({
  path: z.string().min(1).describe('Path relative to the workspace root.'),
  content: z.string().describe('Current content of the open file.')
});

const searchSchema = z.object({
  query: z.string().min(1).describe('Natural-language search query.'),
  model: z.string().min(1).optional().describe('Optional LiteLLM model alias, for example balanced.')
});

const symbolSchema = z.object({
  name: z.string().min(1).describe('Fully qualified or short symbol name.')
});

const depsSchema = z.object({
  name: z.string().min(1).describe('Fully qualified or short symbol name.'),
  depth: z.number().int().positive().max(8).default(2).optional()
});

const moduleSchema = z.object({
  module_name: z.string().min(1).describe('Module name or module path.')
});

function textResult(payload: unknown) {
  return {
    content: [
      {
        type: 'text' as const,
        text: JSON.stringify(payload, null, 2)
      }
    ]
  };
}

function normalizeLocalFiles(
  workspaceRoot: string,
  localFiles: readonly LocalFileInput[] | undefined
): LocalFileInput[] {
  if (!localFiles || localFiles.length === 0) {
    return [];
  }

  return localFiles.map((file) => ({
    path: normalizeWorkspacePath(workspaceRoot, file.path),
    content: file.content
  }));
}

function normalizeChangedFiles(workspaceRoot: string, changedFiles: readonly string[]): string[] {
  return changedFiles.map((filePath) => normalizeWorkspacePath(workspaceRoot, filePath));
}

export function registerTools(
  server: McpServer,
  coreClient: CoreClient,
  config: AdapterConfig
): void {
  const explainSchema = z.object({
    name: z.string().min(1).describe('Fully qualified or short symbol name.'),
    model: z.string().min(1).optional().describe('Optional LiteLLM model alias.'),
    local_files: z.array(localFileSchema).max(config.defaults.maxLocalFiles).optional()
  });

  const workspaceDeltaSchema = z.object({
    changed_files: z
      .array(z.string().min(1))
      .min(1)
      .max(config.defaults.maxChangedFiles)
      .describe('Changed workspace files.'),
    local_files: z.array(localFileSchema).max(config.defaults.maxLocalFiles).optional()
  });

  server.registerTool(
    'search_codebase',
    {
      title: 'Search codebase',
      description:
        'Semantic search across the indexed C++ codebase. Returns ranked modules and symbols.',
      inputSchema: searchSchema
    },
    async ({ query, model }) => textResult(await coreClient.searchCodebase(query, model))
  );

  server.registerTool(
    'get_symbol',
    {
      title: 'Get symbol',
      description:
        'Explain a symbol using the current server index and retrieval context without local IDE files.',
      inputSchema: symbolSchema
    },
    async ({ name }) => textResult(await coreClient.getSymbol(name))
  );

  server.registerTool(
    'get_deps',
    {
      title: 'Get dependencies',
      description:
        'Return callers, callees, and transitive module dependencies for a symbol.',
      inputSchema: depsSchema
    },
    async ({ name, depth }) => textResult(await coreClient.getDeps(name, depth))
  );

  server.registerTool(
    'explain_symbol',
    {
      title: 'Explain symbol',
      description:
        'Explain a symbol with retrieval context and optional local IDE files from the current workspace.',
      inputSchema: explainSchema
    },
    async ({ name, model, local_files }) =>
      textResult(
        await coreClient.explainSymbol(name, normalizeLocalFiles(config.workspaceRoot, local_files), model)
      )
  );

  server.registerTool(
    'find_callers',
    {
      title: 'Find callers',
      description: 'List all direct callers for the requested symbol.',
      inputSchema: symbolSchema
    },
    async ({ name }) => textResult(await coreClient.findCallers(name))
  );

  server.registerTool(
    'get_module_overview',
    {
      title: 'Get module overview',
      description: 'Summarize a module and list its symbols, internal dependencies, and context.',
      inputSchema: moduleSchema
    },
    async ({ module_name }) => textResult(await coreClient.getModuleOverview(module_name))
  );

  server.registerTool(
    'sync_workspace_delta',
    {
      title: 'Sync workspace delta',
      description:
        'Send changed files from the IDE to the Core Engine for incremental reindexing. Optional local files are normalized and echoed back for diagnostics.',
      inputSchema: workspaceDeltaSchema
    },
    async ({ changed_files, local_files }) => {
      const normalizedChangedFiles = normalizeChangedFiles(config.workspaceRoot, changed_files);
      const normalizedLocalFiles = normalizeLocalFiles(config.workspaceRoot, local_files);
      const response = await coreClient.reindex(normalizedChangedFiles);
      return textResult({
        changed_files: normalizedChangedFiles,
        local_files: normalizedLocalFiles,
        core: response
      });
    }
  );
}
