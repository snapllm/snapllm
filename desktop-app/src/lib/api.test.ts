import type { AxiosAdapter, AxiosRequestConfig, AxiosResponse, InternalAxiosRequestConfig } from 'axios';
import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  api,
  StreamingClient,
  listModels,
  promoteContext,
  demoteContext,
  fetchProtectedAsset,
  getContext,
  getRuntimeApiKeyValidationError,
  handleApiError,
  resolveApiBaseUrl,
  scanFolder,
  scanModelsFolder,
  setRuntimeApiKey,
  updateConfig,
} from './api';
import { resolveLoadedModelId } from './modelRouting';

const originalAdapter = api.defaults.adapter;

const captureNextRequest = () => {
  let captured: InternalAxiosRequestConfig | undefined;
  const adapter: AxiosAdapter = async (config): Promise<AxiosResponse> => {
    captured = config;
    return {
      data: { status: 'success', models: [], count: 0, current_model: null },
      status: 200,
      statusText: 'OK',
      headers: {},
      config,
    };
  };
  api.defaults.adapter = adapter;
  return () => captured;
};

afterEach(() => {
  api.defaults.adapter = originalAdapter;
  setRuntimeApiKey('');
  vi.unstubAllGlobals();
});

describe('desktop API contracts', () => {
  it('never routes chat to an unloaded model id', () => {
    expect(resolveLoadedModelId('stale', [{ id: 'resident' }])).toBe('resident');
    expect(resolveLoadedModelId('stale', [{ id: 'one' }, { id: 'two' }])).toBe('');
    expect(resolveLoadedModelId('resident', [{ id: 'resident' }])).toBe('resident');
  });
  const validApiKey = 'k'.repeat(32);

  it('enforces the server API key policy before storing a key', () => {
    expect(getRuntimeApiKeyValidationError('')).toBeNull();
    expect(getRuntimeApiKeyValidationError(validApiKey)).toBeNull();
    expect(getRuntimeApiKeyValidationError('k'.repeat(4096))).toBeNull();
    expect(getRuntimeApiKeyValidationError('k'.repeat(31))).toContain('32-4096');
    expect(getRuntimeApiKeyValidationError('k'.repeat(4097))).toContain('32-4096');
    expect(getRuntimeApiKeyValidationError(`${'k'.repeat(31)} `)).toContain('visible ASCII');
    expect(getRuntimeApiKeyValidationError(`${'k'.repeat(31)}\n`)).toContain('visible ASCII');
    expect(getRuntimeApiKeyValidationError(`${'k'.repeat(31)}é`)).toContain('visible ASCII');
    expect(() => setRuntimeApiKey('short')).toThrow(TypeError);
  });

  it('rejects protected asset URLs outside the trusted image endpoint', async () => {
    setRuntimeApiKey(validApiKey);
    await expect(
      fetchProtectedAsset('https://attacker.example/steal.png'),
    ).rejects.toThrow('outside the SnapLLM image endpoint');
    await expect(
      fetchProtectedAsset('/api/v1/config'),
    ).rejects.toThrow('outside the SnapLLM image endpoint');
  });

  it('uses the serving HTTP origin and keeps the Tauri localhost fallback', () => {
    expect(resolveApiBaseUrl(undefined, {
      origin: 'http://model-host.example:6930',
      protocol: 'http:',
    })).toBe('http://model-host.example:6930');
    expect(resolveApiBaseUrl(undefined, {
      origin: 'tauri://localhost',
      protocol: 'tauri:',
    })).toBe('http://localhost:6930');
    expect(resolveApiBaseUrl('https://api.example', {
      origin: 'http://ignored.example',
      protocol: 'http:',
    })).toBe('https://api.example');
  });

  it('sanitizes Axios errors without exposing request credentials', () => {
    const secret = 'secret-token-that-must-never-be-logged';
    const error = new Error('Request failed') as Error & {
      isAxiosError: boolean;
      config: { headers: { Authorization: string } };
      response: {
        status: number;
        data: { error: { message: string } };
      };
    };
    error.isAxiosError = true;
    error.config = { headers: { Authorization: `Bearer ${secret}` } };
    error.response = {
      status: 400,
      data: { error: { message: 'Invalid request' } },
    };

    const sanitized = handleApiError(error);
    expect(sanitized).toBe('Invalid request');
    expect(sanitized).not.toContain(secret);
    expect(JSON.stringify(sanitized)).not.toContain('Authorization');
  });

  it('treats a React Query invocation as context, not a model filter', async () => {
    const getRequest = captureNextRequest();

    await listModels({
      queryKey: ['models'],
    });

    expect(getRequest()?.params).toEqual({});
  });

  it('treats a React Query invocation as context, not a scan path', async () => {
    const getRequest = captureNextRequest();

    await scanModelsFolder({
      queryKey: ['models', 'scan'],
    });

    expect(getRequest()?.url).toBe('/config');
  });

  it('normalizes Windows model paths returned by folder scans', async () => {
    api.defaults.adapter = async (config): Promise<AxiosResponse> => ({
      data: {
        status: 'success',
        models: [{ path: 'D:\\Models\\tinyllama.Q4_K_M.gguf' }],
        count: 1,
      },
      status: 200,
      statusText: 'OK',
      headers: {},
      config,
    });

    await expect(scanFolder('D:\\Models')).resolves.toEqual([
      'D:/Models/tinyllama.Q4_K_M.gguf',
    ]);
  });

  it('sends the runtime API key as a Bearer header without putting it in the URL', async () => {
    const getRequest = captureNextRequest();
    setRuntimeApiKey(validApiKey);

    await listModels();

    expect(getRequest()?.headers.get('Authorization')).toBe(`Bearer ${validApiKey}`);
    expect(getRequest()?.url).not.toContain(validApiKey);
  });

  it('sends the runtime API key on streaming fetch requests', async () => {
    let requestInit: RequestInit | undefined;
    vi.stubGlobal('fetch', async (_input: RequestInfo | URL, init?: RequestInit) => {
      requestInit = init;
      return new Response('data: [DONE]\n\n', {
        status: 200,
        headers: { 'Content-Type': 'text/event-stream' },
      });
    });
    setRuntimeApiKey(validApiKey);

    await new Promise<void>((resolve, reject) => {
      new StreamingClient().connect(
        { prompt: 'hello' },
        () => undefined,
        reject,
        resolve,
      );
    });

    expect(requestInit?.headers).toMatchObject({
      Authorization: `Bearer ${validApiKey}`,
    });
  });

  it('sends explicit JSON tier bodies for context promotion and demotion', async () => {
    const requests: AxiosRequestConfig[] = [];
    api.defaults.adapter = async (config) => {
      requests.push(config);
      return {
        data: { status: 'success', message: 'changed', current_tier: 'hot' },
        status: 200,
        statusText: 'OK',
        headers: {},
        config,
      };
    };

    await promoteContext('context/with space');
    await demoteContext('context/with space');

    expect(requests.map(({ url, data }) => ({ url, data }))).toEqual([
      { url: '/contexts/context%2Fwith%20space/promote', data: '{"tier":"hot"}' },
      { url: '/contexts/context%2Fwith%20space/demote', data: '{"tier":"cold"}' },
    ]);
  });

  it('unwraps the server context-detail envelope', async () => {
    const expected = {
      id: 'ctx-1',
      name: 'example',
      model_id: 'model-1',
      tier: 'warm',
      token_count: 12,
      size_bytes: 64,
      storage_size_bytes: 64,
      created_at: 0,
      last_accessed: 0,
      access_count: 0,
      cache_hits: 0,
      cache_misses: 0,
      status: 'ready',
    };
    api.defaults.adapter = async (config) => ({
      data: { status: 'success', context: expected },
      status: 200,
      statusText: 'OK',
      headers: {},
      config,
    });

    await expect(getContext('ctx-1')).resolves.toEqual(expected);
  });

  it('preserves the workspace_root field in the settings schema', async () => {
    const getRequest = captureNextRequest();

    await updateConfig({
      workspace: {
        workspace_root: '/srv/snapllm',
        default_models_path: '/srv/models',
      },
    });

    expect(JSON.parse(String(getRequest()?.data))).toEqual({
      workspace: {
        workspace_root: '/srv/snapllm',
        default_models_path: '/srv/models',
      },
    });
  });
});
