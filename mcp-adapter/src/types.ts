export interface AdapterServerConfig {
  host: string;
  port: number;
  path: string;
  messagesPath: string;
  healthPath: string;
}

export interface AdapterAuthConfig {
  apiKeys: string[];
}

export interface AdapterCoreConfig {
  baseUrl: string;
  timeoutMs: number;
  modelHeader: string;
}

export interface AdapterDefaultsConfig {
  defaultModel: string;
  workspaceRoot: string;
  maxLocalFiles: number;
  maxChangedFiles: number;
}

export interface AdapterConfig {
  server: AdapterServerConfig;
  auth: AdapterAuthConfig;
  core: AdapterCoreConfig;
  defaults: AdapterDefaultsConfig;
  configPath: string;
  workspaceRoot: string;
}

export interface LocalFileInput {
  path: string;
  content: string;
}
