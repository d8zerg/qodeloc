import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { SSEServerTransport } from '@modelcontextprotocol/sdk/server/sse.js';
import { CoreClient } from './core-client.js';
import { isAuthorized, extractBearerToken } from './auth.js';
import { registerTools } from './tools.js';
import type { AdapterConfig } from './types.js';

interface SessionContext {
  apiKey: string;
  server: McpServer;
  transport: SSEServerTransport;
}

function jsonResponse(res: ServerResponse, statusCode: number, body: unknown): void {
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store'
  });
  res.end(JSON.stringify(body));
}

async function readBody(req: IncomingMessage, maxBytes = 1_048_576): Promise<unknown> {
  const chunks: Buffer[] = [];
  let total = 0;
  for await (const chunk of req) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    total += buffer.length;
    if (total > maxBytes) {
      throw new Error('Request body exceeds size limit');
    }
    chunks.push(buffer);
  }

  if (chunks.length === 0) {
    return undefined;
  }

  const text = Buffer.concat(chunks).toString('utf8');
  if (!text.trim()) {
    return undefined;
  }

  return JSON.parse(text) as unknown;
}

function createMcpServer(config: AdapterConfig, coreClient: CoreClient): McpServer {
  const server = new McpServer(
    {
      name: 'qodeloc-mcp-adapter',
      version: '0.1.0'
    },
    {
      capabilities: {
        logging: {}
      }
    }
  );

  registerTools(server, coreClient, config);
  return server;
}

export class QodeLocMcpAdapter {
  private readonly coreClient: CoreClient;
  private readonly sessions = new Map<string, SessionContext>();
  private httpServer: Server | undefined;

  constructor(private readonly config: AdapterConfig) {
    this.coreClient = new CoreClient(config);
  }

  async start(): Promise<Server> {
    if (this.httpServer) {
      return this.httpServer;
    }

    this.httpServer = createServer(async (req, res) => {
      try {
        await this.route(req, res);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        jsonResponse(res, 500, { error: message });
      }
    });

    await new Promise<void>((resolve, reject) => {
      const onError = (error: Error) => reject(error);
      this.httpServer?.once('error', onError);
      this.httpServer?.listen(this.config.server.port, this.config.server.host, () => {
        this.httpServer?.off('error', onError);
        resolve();
      });
    });

    return this.httpServer;
  }

  async stop(): Promise<void> {
    const server = this.httpServer;
    this.httpServer = undefined;

    for (const [sessionId, session] of this.sessions.entries()) {
      try {
        await session.server.close();
      } catch {
        // Ignore shutdown noise.
      }
      this.sessions.delete(sessionId);
    }

    if (!server) {
      return;
    }

    await new Promise<void>((resolve, reject) => {
      server.close((error) => {
        if (error) {
          reject(error);
        } else {
          resolve();
        }
      });
    });
  }

  private async route(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const url = new URL(req.url ?? '/', `http://${req.headers.host ?? `${this.config.server.host}:${this.config.server.port}`}`);
    const path = url.pathname;

    if (path === this.config.server.healthPath && req.method === 'GET') {
      const payload = {
        status: 'ok',
        sessions: this.sessions.size,
        workspace_root: this.config.workspaceRoot,
        core_url: this.config.core.baseUrl
      };
      jsonResponse(res, 200, payload);
      return;
    }

    if (path === this.config.server.path && req.method === 'GET') {
      await this.handleSse(req, res);
      return;
    }

    if (path === this.config.server.messagesPath && req.method === 'POST') {
      await this.handleMessages(req, res, url);
      return;
    }

    jsonResponse(res, 404, { error: `Unknown endpoint: ${path}` });
  }

  private async handleSse(req: IncomingMessage, res: ServerResponse): Promise<void> {
    if (!isAuthorized(req.headers.authorization, this.config.auth.apiKeys)) {
      jsonResponse(res, 401, { error: 'Unauthorized' });
      return;
    }

    const apiKey = extractBearerToken(req.headers.authorization);
    const transport = new SSEServerTransport(this.config.server.messagesPath, res);
    const server = createMcpServer(this.config, this.coreClient);
    const sessionId = transport.sessionId;
    const session: SessionContext = { apiKey, server, transport };

    this.sessions.set(sessionId, session);
    transport.onclose = () => {
      this.sessions.delete(sessionId);
    };

    await server.connect(transport);
  }

  private async handleMessages(
    req: IncomingMessage,
    res: ServerResponse,
    url: URL
  ): Promise<void> {
    const sessionId = url.searchParams.get('sessionId') ?? '';
    if (!sessionId) {
      jsonResponse(res, 400, { error: 'Missing sessionId query parameter' });
      return;
    }

    const session = this.sessions.get(sessionId);
    if (!session) {
      jsonResponse(res, 404, { error: 'Session not found' });
      return;
    }

    if (!isAuthorized(req.headers.authorization, [session.apiKey])) {
      jsonResponse(res, 401, { error: 'Unauthorized' });
      return;
    }

    const parsedBody = await readBody(req);
    await session.transport.handlePostMessage(req, res, parsedBody);
  }
}

export function createAdapter(config: AdapterConfig): QodeLocMcpAdapter {
  return new QodeLocMcpAdapter(config);
}
