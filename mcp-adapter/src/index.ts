import { loadAdapterConfig } from './config.js';
import { createAdapter } from './server.js';

async function main(): Promise<void> {
  const config = await loadAdapterConfig();
  const adapter = createAdapter(config);
  const server = await adapter.start();
  const address = server.address();
  const port = typeof address === 'object' && address !== null ? address.port : config.server.port;

  console.log(
    `[qodeloc-mcp] listening on ${config.server.host}:${port} (core=${config.core.baseUrl}, workspace=${config.workspaceRoot})`
  );

  const shutdown = async () => {
    await adapter.stop();
  };

  process.on('SIGINT', () => {
    void shutdown().finally(() => process.exit(0));
  });
  process.on('SIGTERM', () => {
    void shutdown().finally(() => process.exit(0));
  });
}

main().catch((error) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`[qodeloc-mcp] failed to start: ${message}`);
  process.exit(1);
});
