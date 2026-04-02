import fetch, { type RequestInit as FetchRequestInit } from 'node-fetch';
import { setTimeout as delay } from 'node:timers/promises';
import type { AdapterConfig, LocalFileInput } from './types.js';

type JsonObject = Record<string, unknown>;

export class CoreClient {
  constructor(private readonly config: AdapterConfig) {}

  async searchCodebase(query: string, model?: string): Promise<JsonObject> {
    return this.requestJson('/search', { query, ...(model ? { model } : {}) }, model);
  }

  async getSymbol(name: string, model?: string): Promise<JsonObject> {
    return this.requestJson('/explain', { name, ...(model ? { model } : {}) }, model);
  }

  async explainSymbol(
    name: string,
    localFiles: readonly LocalFileInput[],
    model?: string
  ): Promise<JsonObject> {
    return this.requestJson(
      '/explain',
      {
        name,
        local_files: localFiles,
        ...(model ? { model } : {})
      },
      model
    );
  }

  async getDeps(name: string, depth?: number, model?: string): Promise<JsonObject> {
    return this.requestJson(
      '/deps',
      { name, ...(typeof depth === 'number' ? { depth } : {}), ...(model ? { model } : {}) },
      model
    );
  }

  async findCallers(name: string, model?: string): Promise<JsonObject> {
    return this.requestJson('/callers', { name, ...(model ? { model } : {}) }, model);
  }

  async getModuleOverview(moduleName: string, model?: string): Promise<JsonObject> {
    return this.requestJson('/module', { module_name: moduleName, ...(model ? { model } : {}) }, model);
  }

  async reindex(changedFiles: readonly string[]): Promise<JsonObject> {
    return this.requestJson('/reindex', { changed_files: [...changedFiles] });
  }

  async status(): Promise<JsonObject> {
    return this.requestJson('/status', undefined);
  }

  private async requestJson(
    path: string,
    body: JsonObject | undefined,
    model?: string
  ): Promise<JsonObject> {
    const url = new URL(path, this.config.core.baseUrl);
    const headers: Record<string, string> = {
      Accept: 'application/json'
    };
    if (body !== undefined) {
      headers['Content-Type'] = 'application/json';
    }
    const resolvedModel = model ?? this.config.defaults.defaultModel;
    if (resolvedModel) {
      headers[this.config.core.modelHeader] = resolvedModel;
    }

    const timeoutSignal = AbortSignal.timeout(this.config.core.timeoutMs);
    const requestInit: FetchRequestInit = {
      method: body === undefined ? 'GET' : 'POST',
      headers,
      signal: timeoutSignal
    };
    if (body !== undefined) {
      requestInit.body = JSON.stringify(body);
    }

    const response = await fetch(url, requestInit);

    if (!response.ok) {
      const errorText = await response.text();
      throw new Error(
        `Core request ${path} failed with HTTP ${response.status}: ${errorText || response.statusText}`
      );
    }

    return (await response.json()) as JsonObject;
  }
}

export async function waitForCoreReady(coreClient: CoreClient, attempts = 30): Promise<void> {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    try {
      const status = await coreClient.status();
      if (status.bootstrap_state === 'error') {
        throw new Error(
          `Core bootstrap failed: ${String(status.bootstrap_message ?? 'unknown error')}`
        );
      }
      if (status.bootstrap_complete === true || status.bootstrap_state === 'ready') {
        return;
      }
    } catch (error) {
      if (error instanceof Error && error.message.startsWith('Core bootstrap failed:')) {
        throw error;
      }
      // Ignore startup race.
    }

    await delay(1000);
  }

  throw new Error('Core Engine did not become ready in time');
}
