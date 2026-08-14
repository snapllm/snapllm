/**
 * SnapLLM local API client
 * TypeScript client for the SnapLLM daemon and HTTP API
 */

import axios, { AxiosError } from 'axios';
import { open } from '@tauri-apps/plugin-dialog';
import { readDir, copyFile, exists } from '@tauri-apps/plugin-fs';

type BrowserLocation = Pick<Location, 'origin' | 'protocol'>;

export const resolveApiBaseUrl = (
  configuredUrl: string | undefined,
  browserLocation: BrowserLocation | undefined,
): string => {
  if (configuredUrl) return configuredUrl;
  if (
    browserLocation
    && (browserLocation.protocol === 'http:' || browserLocation.protocol === 'https:')
  ) {
    return browserLocation.origin;
  }
  return 'http://localhost:6930';
};

// Browser builds use the serving origin. Tauri/custom-protocol builds fall
// back to the local CLI server unless VITE_API_URL explicitly overrides it.
const API_BASE_URL = resolveApiBaseUrl(
  import.meta.env.VITE_API_URL,
  typeof window === 'undefined' ? undefined : window.location,
);
const API_TRANSPORT_BASE_URL =
  import.meta.env.DEV && !import.meta.env.VITE_API_URL ? '' : API_BASE_URL;
const API_V1 = `${API_TRANSPORT_BASE_URL}/api/v1`;
let runtimeApiKey = '';
const MIN_API_KEY_LENGTH = 32;
const MAX_API_KEY_LENGTH = 4096;

type QueryFunctionInvocation = {
  queryKey: readonly unknown[];
};

const isQueryFunctionInvocation = (value: unknown): value is QueryFunctionInvocation => (
  typeof value === 'object'
  && value !== null
  && 'queryKey' in value
);

const getRuntimeHeaders = (): Record<string, string> => (
  runtimeApiKey ? { Authorization: `Bearer ${runtimeApiKey}` } : {}
);
export const getRuntimeRequestHeaders = (): Record<string, string> => getRuntimeHeaders();

/**
 * Set the API key used for subsequent requests in this renderer session.
 *
 * The key is held in memory only. It is not written to config, storage, URLs,
 * logs, or error messages.
 */
export const setRuntimeApiKey = (apiKey: string): void => {
  const validationError = getRuntimeApiKeyValidationError(apiKey);
  if (validationError) {
    throw new TypeError(validationError);
  }
  runtimeApiKey = apiKey;
};

export const getRuntimeApiKeyValidationError = (apiKey: string): string | null => {
  if (apiKey.length === 0) {
    return null;
  }
  if (apiKey.length < MIN_API_KEY_LENGTH || apiKey.length > MAX_API_KEY_LENGTH) {
    return `API key must be ${MIN_API_KEY_LENGTH}-${MAX_API_KEY_LENGTH} characters.`;
  }
  if (!/^[\x21-\x7E]+$/.test(apiKey)) {
    return 'API key must contain visible ASCII characters only, without spaces.';
  }
  return null;
};

// ============================================================================
// Model Type Detection (mirrors server-side logic)
// ============================================================================

export type ModelType = 'llm' | 'diffusion' | 'vision' | 'unknown';

/**
 * Detect model type from file path (client-side, mirrors server logic)
 */
export function detectModelType(filePath: string): ModelType {
  const lowerPath = filePath.toLowerCase();

  // Diffusion model indicators
  if (
    lowerPath.includes('stable-diffusion') ||
    lowerPath.includes('sd_') ||
    lowerPath.includes('sd1') ||
    lowerPath.includes('sd2') ||
    lowerPath.includes('sdxl') ||
    lowerPath.includes('flux') ||
    lowerPath.includes('unet') ||
    lowerPath.endsWith('.safetensors')
  ) {
    return 'diffusion';
  }

  // Vision/Multimodal model indicators
  if (
    lowerPath.includes('llava') ||
    lowerPath.includes('qwen-vl') ||
    lowerPath.includes('moondream') ||
    lowerPath.includes('bakllava') ||
    lowerPath.includes('minicpm-v')
  ) {
    return 'vision';
  }

  // Default to LLM for .gguf files
  if (lowerPath.endsWith('.gguf')) {
    return 'llm';
  }

  return 'unknown';
}

/**
 * Get human-readable model type label
 */
export function getModelTypeLabel(type: ModelType): string {
  switch (type) {
    case 'llm': return 'Text LLM';
    case 'diffusion': return 'Image Diffusion';
    case 'vision': return 'Vision/Multimodal';
    default: return 'Unknown';
  }
}

/**
 * Get model type badge color
 */
export function getModelTypeBadgeColor(type: ModelType): 'brand' | 'success' | 'warning' | 'error' | 'default' {
  switch (type) {
    case 'llm': return 'brand';
    case 'diffusion': return 'success';
    case 'vision': return 'warning';
    default: return 'default';
  }
}

// Axios instance with default config
export const api = axios.create({
  baseURL: API_V1,
  headers: {
    'Content-Type': 'application/json',
  },
  timeout: 300000, // 5 minutes
});

api.interceptors.request.use((config) => {
  if (runtimeApiKey) {
    config.headers.set('Authorization', `Bearer ${runtimeApiKey}`);
  }
  return config;
});

// ============================================================================
// Types matching the SnapLLM local API schemas
// ============================================================================

export interface HealthResponse {
  status: string;
  version: string;
  timestamp: string;
}

export interface ModelInfo {
  id: string;
  name: string;
  active: boolean;
  type?: 'llm' | 'diffusion' | 'vision';  // Model type
  domain?: string;
  engine?: string;
  strategy?: string;
  device?: string;
  status?: string;
  ram_usage_mb?: number;
  load_time_ms?: number;
  cache_size_gb?: number;
  requests_per_hour?: number;
  avg_latency_ms?: number;
  throughput_toks?: number;
  loaded_at?: string;
}

export interface ModelListResponse {
  status: string;
  models: ModelInfo[];
  count: number;
  current_model: string | null;
}

export interface ModelLoadRequest {
  model_id: string;
  file_path: string;
  strategy?: string;
  cache_only?: boolean;
  domain?: string;
  model_type?: 'auto' | 'llm' | 'diffusion' | 'vision';  // Model type for routing
  // Multi-file model paths (for SD3, FLUX, Wan2)
  vae_path?: string;
  t5xxl_path?: string;
  clip_l_path?: string;
  clip_g_path?: string;
  clip_vision_path?: string;
  high_noise_model_path?: string;
  // Vision model paths
  mmproj_path?: string;
  offload_to_cpu?: boolean;
}

export interface ModelLoadResponse {
  status: string;
  message: string;
  model: string;
  load_time_ms: number;
  cache_only: boolean;
}

export interface ModelSwitchRequest {
  name: string;
}

export interface ModelSwitchResponse {
  status: string;
  message: string;
  model: string;
  switch_time_ms: number;
}

export interface GenerateRequest {
  prompt: string;
  max_tokens?: number;
  model?: string;
  temperature?: number;
  top_p?: number;
  stream?: boolean;
  use_context_cache?: boolean; // vPID L2: Enable context KV cache
}

export interface GenerateResponse {
  status: string;
  prompt: string;
  generated_text: string;
  model: string;
  max_tokens: number;
  generation_time_s: number;
  tokens_per_second: number;
}

export interface BatchChatMessage {
  role: 'system' | 'user' | 'assistant';
  content: string;
}

export interface BatchItemRequest {
  prompt?: string;
  messages?: BatchChatMessage[];
  system_prompt?: string;
  max_tokens?: number;
  temperature?: number;
  top_p?: number;
  top_k?: number;
  repeat_penalty?: number;
}

export interface BatchGenerateRequest {
  prompts?: string[];           // Legacy format
  items?: BatchItemRequest[];   // New parallel format with per-prompt params
  max_tokens?: number;
  temperature?: number;
  top_p?: number;
  top_k?: number;
  repeat_penalty?: number;
  model?: string;
}

export interface BatchResultItem {
  index: number;
  prompt: string;
  generated_text: string;
  tokens_generated: number;
  latency_ms: number;
  success: boolean;
  error?: string;
}

export interface BatchGenerateResponse {
  status: string;
  results: BatchResultItem[];
  model: string;
  total_prompts: number;
  successful: number;
  total_time_s: number;
  avg_time_per_prompt_s: number;
  parallel_sequences: number;
}

export interface StreamMessage {
  role: 'system' | 'user' | 'assistant';
  content: string;
}

export interface StreamRequest {
  prompt?: string;  // Legacy: single prompt (will be wrapped as user message)
  messages?: StreamMessage[];  // Preferred: full conversation history for context caching
  max_tokens?: number;
  model?: string;
  use_context_cache?: boolean; // vPID L2
}

export interface StreamToken {
  token: string;
  index: number;
  finish_reason: string | null;
}

export interface ErrorResponse {
  detail?: string;
  error?: {
    message?: string;
    type?: string;
    code?: number;
  };
}

export interface CacheStats {
  model_id: string;
  processing_hits: number;
  processing_misses: number;
  processing_hit_rate: number;
  generation_hits: number;
  generation_misses: number;
  generation_hit_rate: number;
  estimated_speedup: number;
  memory_usage_mb: number;
  workspace_total_mb?: number;
  workspace_used_mb?: number;
  tensor_cache_used_mb?: number;
  tensor_cache_budget_mb?: number;
  tensor_cache_utilization?: number;
  tensor_cache_hit_rate?: number;
  cached_tensor_count?: number;
}

export interface CacheStatsResponse {
  status: string;
  models: CacheStats[];
  total_memory_mb?: number;
  summary?: {
    total_models: number;
    current_model: string;
    total_memory_mb: number;
    total_cache_hits: number;
    total_cache_misses: number;
    global_hit_rate: number;
    total_reads: number;
    total_writes: number;
    total_bytes_read_mb: number;
    total_bytes_written_mb: number;
    average_speedup: number;
  };
}

// ============================================================================
// Health & Server Status
// ============================================================================

export const getHealth = async (): Promise<HealthResponse> => {
  const { data } = await api.get('/health', {
    baseURL: API_TRANSPORT_BASE_URL, // Health is at root
    timeout: 5000,
  });
  return data;
};

// ============================================================================
// Model Management
// ============================================================================

export type ModelTypeFilter = 'llm' | 'diffusion' | 'vision' | 'text' | 'sd' | 'image';

export const listModels = async (
  typeFilterOrQueryContext?: ModelTypeFilter | QueryFunctionInvocation
): Promise<ModelListResponse> => {
  const typeFilter = isQueryFunctionInvocation(typeFilterOrQueryContext)
    ? undefined
    : typeFilterOrQueryContext;
  const params = typeFilter ? { type: typeFilter } : {};
  const { data } = await api.get('/models/', { params });
  return data;
};

// Convenience functions for specific model types
export const listLLMModels = async (): Promise<ModelListResponse> => listModels('llm');
export const listDiffusionModels = async (): Promise<ModelListResponse> => listModels('diffusion');
export const listVisionModels = async (): Promise<ModelListResponse> => listModels('vision');

export const loadModel = async (request: ModelLoadRequest): Promise<ModelLoadResponse> => {
  const { data } = await api.post('/models/load', request, {
    timeout: 600000, // 10 minutes
  });
  return data;
};

export const switchModel = async (request: ModelSwitchRequest): Promise<ModelSwitchResponse> => {
  const { data } = await api.post('/models/switch', request);
  return data;
};

export const getCacheStats = async (): Promise<CacheStatsResponse> => {
  const { data } = await api.get('/models/cache/stats');
  return data;
};

export const clearCache = async (): Promise<{ status: string; message: string }> => {
  const { data } = await api.post('/models/cache/clear');
  return data;
};

// ============================================================================
// Text Generation
// ============================================================================

export const generateText = async (request: GenerateRequest): Promise<GenerateResponse> => {
  const { data } = await api.post('/generate', request);
  return data;
};

export const generateBatch = async (request: BatchGenerateRequest): Promise<BatchGenerateResponse> => {
  const { data } = await api.post('/generate/batch', request);
  return data;
};

// ============================================================================
// Image Generation
// ============================================================================

export interface ImageGenerateRequest {
  prompt: string;
  negative_prompt?: string;
  model?: string;
  use_context_cache?: boolean; // vPID L2
  width?: number;
  height?: number;
  steps?: number;
  cfg_scale?: number;
  seed?: number;
  sampler?: string;
  batch_size?: number;
}

export interface ImageGenerateResponse {
  status: string;
  images: string[];
  prompt: string;
  model: string;
  generation_time_s: number;
  seed: number;
  width: number;
  height: number;
}

export const generateImage = async (request: ImageGenerateRequest): Promise<ImageGenerateResponse> => {
  const { data } = await api.post('/diffusion/generate', request, {
    timeout: 600000, // 10 minutes for image generation
  });
  return data;
};

/**
 * Download a protected server asset with the in-memory API key and expose it
 * as a renderer-local blob URL. Callers own the returned URL and must revoke it.
 */
export const fetchProtectedAsset = async (sourceUrl: string): Promise<string> => {
  const trustedBase = new URL(API_BASE_URL);
  const parsedUrl = new URL(sourceUrl, trustedBase);
  if (
    parsedUrl.origin !== trustedBase.origin
    || !/^\/api\/v1\/images\/[^/]+\.png$/.test(parsedUrl.pathname)
  ) {
    throw new TypeError('Protected asset URL is outside the SnapLLM image endpoint');
  }
  let requestUrl = parsedUrl.toString();
  if (import.meta.env.DEV && !import.meta.env.VITE_API_URL) {
    requestUrl = parsedUrl.pathname;
  }
  const response = await axios.get<Blob>(requestUrl, {
    baseURL: API_TRANSPORT_BASE_URL,
    headers: getRuntimeHeaders(),
    responseType: 'blob',
    timeout: 300000,
  });
  return URL.createObjectURL(response.data);
};

export interface DiffusionModel {
  id: string;
  name: string;
  type: string;
  subtype: string;
  path: string;
  size_bytes: number;
  size_gb: number;
  status: string;
}

export interface DiffusionModelsResponse {
  status: string;
  models: DiffusionModel[];
  count: number;
}

// Note: listDiffusionModels is defined above using the unified /models?type=diffusion endpoint

// Vision (multimodal) generation
export interface VisionRequest {
  model: string;
  images: string[]; // base64 encoded images (array)
  prompt: string;
  max_tokens?: number;
  temperature?: number;
  top_p?: number;
  top_k?: number;
  repeat_penalty?: number;
}

export interface VisionResponse {
  status: string;
  response: string;
  model: string;
  generation_time_s: number;
  tokens_per_second: number;
}

export const generateVision = async (request: VisionRequest): Promise<VisionResponse> => {
  const { data } = await api.post('/vision/generate', request, {
    timeout: 300000,
  });
  return data;
};

// ============================================================================
// SSE Streaming (Server-Sent Events)
// Uses /v1/chat/completions with stream: true
// ============================================================================

export class StreamingClient {
  private abortController: AbortController | null = null;
  private onTokenCallback: ((token: StreamToken) => void) | null = null;
  private onErrorCallback: ((error: Error) => void) | null = null;
  private onCompleteCallback: (() => void) | null = null;
  private totalTokens: number = 0;
  private isActive: boolean = false;

  connect(
    request: StreamRequest,
    onToken: (token: StreamToken) => void,
    onError: (error: Error) => void,
    onComplete: () => void
  ): void {
    this.onTokenCallback = onToken;
    this.onErrorCallback = onError;
    this.onCompleteCallback = onComplete;
    this.totalTokens = 0;
    this.isActive = true;

    // Create abort controller for cancellation
    this.abortController = new AbortController();

    // Use fetch with SSE for streaming via /v1/chat/completions
    const streamUrl = `${API_TRANSPORT_BASE_URL}/v1/chat/completions`;

    console.log('[StreamingClient] Starting SSE stream to', streamUrl);

    // Build messages array: prefer explicit messages, fallback to wrapping prompt
    const messages = request.messages || [{ role: 'user' as const, content: request.prompt || '' }];

    fetch(streamUrl, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Accept': 'text/event-stream',
        ...getRuntimeHeaders(),
      },
      body: JSON.stringify({
        ...(request.model ? { model: request.model } : {}),
        messages,
        max_tokens: request.max_tokens || 512,
        stream: true,
        use_context_cache: request.use_context_cache ?? true,  // Enable by default
      }),
      signal: this.abortController.signal,
    })
      .then(async (response) => {
        if (!response.ok) {
          let detail = '';
          try {
            const payload = await response.clone().json() as { error?: { message?: string } | string; message?: string };
            detail = typeof payload.error === 'string'
              ? payload.error
              : payload.error?.message || payload.message || '';
          } catch {
            // Some proxies return an empty body for validation failures.
          }
          throw new Error(`HTTP ${response.status}: ${detail || response.statusText}`);
        }

        const reader = response.body?.getReader();
        if (!reader) {
          throw new Error('No response body reader available');
        }

        const decoder = new TextDecoder();
        let buffer = '';

        while (this.isActive) {
          const { done, value } = await reader.read();

          if (done) {
            console.log('[StreamingClient] Stream ended');
            break;
          }

          buffer += decoder.decode(value, { stream: true });

          // Process SSE events (data: {...}\n\n format)
          const lines = buffer.split('\n');
          buffer = lines.pop() || ''; // Keep incomplete line in buffer

          for (const line of lines) {
            if (line.startsWith('data: ')) {
              const data = line.slice(6).trim();

              if (data === '[DONE]') {
                console.log('[StreamingClient] Received [DONE]');
                this.isActive = false;
                this.onCompleteCallback?.();
                return;
              }

              try {
                const parsed = JSON.parse(data);

                // OpenAI format: choices[0].delta.content
                const content = parsed.choices?.[0]?.delta?.content;
                const finishReason = parsed.choices?.[0]?.finish_reason;

                if (content) {
                  this.totalTokens++;
                  this.onTokenCallback?.({
                    token: content,
                    index: this.totalTokens,
                    finish_reason: finishReason || null,
                  });
                }

                if (finishReason === 'stop') {
                  console.log('[StreamingClient] Finish reason: stop');
                  this.isActive = false;
                  this.onCompleteCallback?.();
                  return;
                }
              } catch (parseError) {
                console.warn('[StreamingClient] Parse error:', handleApiError(parseError));
              }
            }
          }
        }

        // Stream completed
        if (this.isActive) {
          this.onCompleteCallback?.();
        }
      })
      .catch((error) => {
        if (error.name === 'AbortError') {
          console.log('[StreamingClient] Stream aborted by user');
        } else {
          console.error('[StreamingClient] Stream error:', handleApiError(error));
          this.onErrorCallback?.(error);
        }
      });
  }

  close(): void {
    this.isActive = false;
    if (this.abortController) {
      this.abortController.abort();
      this.abortController = null;
    }
  }

  isConnected(): boolean {
    return this.isActive;
  }
}

// ============================================================================
// Error Handling
// ============================================================================

export const handleApiError = (error: unknown): string => {
  if (axios.isAxiosError(error)) {
    const axiosError = error as AxiosError<ErrorResponse>;

    if (axiosError.response?.data?.detail) {
      return axiosError.response.data.detail;
    }
    if (axiosError.response?.data?.error?.message) {
      return axiosError.response.data.error.message;
    }

    if (axiosError.response?.status === 404) {
      return 'API endpoint not found. Is the server running?';
    }

    if (axiosError.response?.status === 500) {
      return 'Internal server error. Check server logs.';
    }

    if (axiosError.code === 'ECONNREFUSED') {
      return 'Cannot connect to the SnapLLM daemon. Open SnapLLM Desktop or start `snapllm --daemon --port 6930`.';
    }

    if (axiosError.code === 'ETIMEDOUT') {
      return 'Request timed out.';
    }

    return axiosError.message || 'Unknown error';
  }

  return String(error);
};

// ============================================================================
// File Dialog Utilities (Tauri)
// ============================================================================

// Check if running in Tauri environment
export const isTauriAvailable = (): boolean => {
  return typeof window !== 'undefined'
    && ('__TAURI__' in window || globalThis.location?.protocol === 'tauri:' || globalThis.location?.protocol === 'asset:');
};

export const selectModelFile = async (): Promise<string | null> => {
  if (!isTauriAvailable()) {
    console.warn('[selectModelFile] Tauri not available - use manual path entry');
    return null;
  }

  try {
    const selected = await open({
      multiple: false,
      filters: [{
        name: 'GGUF Models',
        extensions: ['gguf']
      }],
      title: 'Select GGUF Model File'
    });

    if (Array.isArray(selected)) {
      return selected[0] || null;
    }
    return selected;
  } catch (error) {
    console.error('[selectModelFile] Error:', handleApiError(error));
    return null;
  }
};

export const selectModelsFolder = async (): Promise<string | null> => {
  if (!isTauriAvailable()) {
    console.warn('[selectModelsFolder] Tauri not available - use manual path entry');
    return null;
  }

  try {
    const selected = await open({
      directory: true,
      multiple: false,
      title: 'Select Models Folder'
    });

    if (Array.isArray(selected)) {
      return selected[0] || null;
    }
    return selected;
  } catch (error) {
    console.error('[selectModelsFolder] Error:', handleApiError(error));
    return null;
  }
};

// Model info from folder scan
export interface ScannedModel {
  path: string;
  filename: string;
  name: string;
  size_bytes: number;
  size_gb: number;
  quantization: string;
}

const normalizeModelPath = (value: string): string => value.replace(/\\/g, '/');

export const scanFolder = async (folderPath: string): Promise<string[]> => {
  console.log('[scanFolder] Scanning folder:', folderPath);

  // First try backend API (works in both browser and Tauri)
  try {
    console.log('[scanFolder] Trying backend API at:', `${API_V1}/models/scan`);
    const { data } = await api.post('/models/scan', { path: folderPath });
    console.log('[scanFolder] Backend response:', data);

    if (data.models && Array.isArray(data.models)) {
      // Return file paths (for backwards compatibility)
      const paths = data.models
        .map((m: ScannedModel) => normalizeModelPath(m.path))
        .filter(Boolean);
      console.log('[scanFolder] Found models:', paths.length);
      return paths;
    }

    if (data.error) {
      console.error('[scanFolder] Backend error:', data.error);
    }
  } catch (error: any) {
    console.warn('[scanFolder] Backend scan failed:', handleApiError(error));
    console.warn('[scanFolder] Falling back to Tauri...');
  }

  // Fallback to Tauri file system if available
  if (isTauriAvailable()) {
    console.log('[scanFolder] Trying Tauri readDir...');
    try {
      const entries = await readDir(folderPath);

      // Filter for .gguf files and return full paths
      const ggufFiles = entries
        .filter(entry => entry.name?.endsWith('.gguf'))
        .map(entry => normalizeModelPath(`${folderPath}/${entry.name}`));

      console.log('[scanFolder] Tauri found:', ggufFiles.length, 'models');
      return ggufFiles;
    } catch (error) {
      console.error('[scanFolder] Tauri readDir failed:', handleApiError(error));
    }
  } else {
    console.log('[scanFolder] Tauri not available');
  }

  console.warn('[scanFolder] No method available to scan folder - server may be offline');
  return [];
};

// Extended scan that returns full model info
export const scanFolderExtended = async (folderPath: string): Promise<ScannedModel[]> => {
  try {
    const { data } = await api.post('/models/scan', { path: folderPath });
    if (data.models && Array.isArray(data.models)) {
      return data.models;
    }
  } catch (error) {
    console.warn('[scanFolderExtended] Backend scan failed:', handleApiError(error));
  }
  return [];
};

// ============================================================================
// Additional API Functions (Stubs for future implementation)
// ============================================================================

export const sendChatMessage = async (params: {
  model?: string;
  messages: Array<{ role: string; content: string }>;
  max_tokens?: number;
  temperature?: number;
  top_p?: number;
  top_k?: number;
  repeat_penalty?: number;
  presence_penalty?: number;
  frequency_penalty?: number;
  seed?: number;
  stop?: string[];
  stream?: boolean;
  use_context_cache?: boolean; // vPID L2: Enable context KV cache
}): Promise<any> => {
  const { data } = await api.post(
    '/v1/chat/completions',
    params,
    {
      baseURL: API_TRANSPORT_BASE_URL,
      timeout: 300000, // 5 minutes
    }
  );
  return data;
};

export const getServerStatus = async (): Promise<any> => {
  try {
    const { data } = await api.get('/server/metrics');
    return { status: data.status || 'ok', models_loaded: data.models?.length || 0, metrics: data };
  } catch (error) {
    // Fall back to health endpoint for CLI server mode
    try {
      const { data } = await api.get('/health', { baseURL: API_TRANSPORT_BASE_URL });
      return { status: data.status, models_loaded: data.models_loaded };
    } catch {
      return { status: 'offline', models_loaded: 0 };
    }
  }
};

export const unloadModel = async (name: string): Promise<any> => {
  const encodedName = encodeURIComponent(name);
  const { data } = await api.delete(`/models/${encodedName}`);
  return data;
};

export const getMetrics = async (): Promise<any> => {
  try {
    const { data } = await api.get('/server/metrics');
    return data;
  } catch (error) {
    // Return empty metrics if endpoint doesn't exist
    return {
      models: [], total_requests: 0, total_tokens_generated: 0, total_errors: 0, uptime_seconds: 0,
      scheduler: { active_inferences: 0, max_active_inferences: 0, waiting_inferences: 0, admission: 'offline' },
    };
  }
};

export const getPopularModels = async (): Promise<string[]> => {
  // Popular GGUF models from HuggingFace
  return [
    "TheBloke/Llama-2-7B-Chat-GGUF",
    "TheBloke/Mistral-7B-Instruct-v0.2-GGUF",
    "TheBloke/CodeLlama-7B-Instruct-GGUF",
    "TheBloke/Llama-2-13B-chat-GGUF",
    "TheBloke/openchat-3.5-GGUF",
    "TheBloke/Mixtral-8x7B-Instruct-v0.1-GGUF",
    "TheBloke/Llama-2-70B-Chat-GGUF",
    "TheBloke/Yi-34B-Chat-GGUF",
  ];
};

// Default SnapLLM workspace paths (local-only dev defaults)
const isWindows = typeof navigator !== 'undefined' && navigator.userAgent.toLowerCase().includes('windows');
const DEFAULT_WORKSPACE_PATH = import.meta.env.VITE_SNAPLLM_WORKSPACE
  || (isWindows ? 'C:/SnapLLM_Workspace' : '/tmp/SnapLLM_Workspace');
const DEFAULT_MODELS_PATH = import.meta.env.VITE_SNAPLLM_MODELS
  || (isWindows ? 'C:/Models' : '/tmp/Models');
const DEFAULT_VPID_CACHE_PATH = `${DEFAULT_WORKSPACE_PATH}/vpid_cache`;

export const getDefaultWorkspacePath = (): string => DEFAULT_WORKSPACE_PATH;
export const getDefaultModelsPath = (): string => DEFAULT_MODELS_PATH;
export const getVPIDCachePath = (): string => DEFAULT_VPID_CACHE_PATH;

// Workspace folder info
export interface WorkspaceFolderInfo {
  path: string;
  name: string;
  exists: boolean;
  files: number;
  sizeEstimate?: string;
}

// Get workspace folders info
export const getWorkspaceFolders = async (): Promise<WorkspaceFolderInfo[]> => {
  let workspaceRoot = DEFAULT_WORKSPACE_PATH;
  let modelsPath = DEFAULT_MODELS_PATH;
  try {
    const cfg = await getConfig();
    if (cfg?.workspace_root) workspaceRoot = cfg.workspace_root.replace(/\\/g, '/');
    if (cfg?.default_models_path) modelsPath = cfg.default_models_path.replace(/\\/g, '/');
  } catch {
    // Ignore and use defaults
  }

  const vpidPath = `${workspaceRoot}/vpid_cache`;
  const folders = [
    { path: vpidPath, name: 'vPID Tensor Cache' },
    { path: modelsPath, name: `Models (${modelsPath})` },
    { path: `${workspaceRoot}/logs`, name: 'Logs' },
    { path: `${workspaceRoot}/config`, name: 'Configuration' },
  ];

  const results: WorkspaceFolderInfo[] = [];

  for (const folder of folders) {
    try {
      const folderExists = await exists(folder.path);
      let fileCount = 0;

      if (folderExists) {
        try {
          const entries = await readDir(folder.path);
          fileCount = entries.length;
        } catch {
          fileCount = 0;
        }
      }

      results.push({
        path: folder.path,
        name: folder.name,
        exists: folderExists,
        files: fileCount,
      });
    } catch (error) {
      console.error(
        `[getWorkspaceFolders] Error checking ${folder.path}:`,
        handleApiError(error),
      );
      results.push({
        path: folder.path,
        name: folder.name,
        exists: false,
        files: 0,
      });
    }
  }

  return results;
};

// Delete a workspace folder
export const deleteWorkspaceFolder = async (folderPath: string): Promise<boolean> => {
  void folderPath;
  throw new Error(
    'Recursive workspace deletion is disabled. Remove the selected folder outside SnapLLM after stopping the server.',
  );
};

// Clear all vPID cache (tensors)
export const clearVPIDCache = async (): Promise<boolean> => {
  let workspaceRoot = DEFAULT_WORKSPACE_PATH;
  try {
    const cfg = await getConfig();
    if (cfg?.workspace_root) workspaceRoot = cfg.workspace_root.replace(/\\/g, '/');
  } catch {
    // Ignore and use default
  }
  return deleteWorkspaceFolder(`${workspaceRoot}/vpid_cache`);
};

// Clear entire workspace
export const clearEntireWorkspace = async (): Promise<boolean> => {
  let workspaceRoot = DEFAULT_WORKSPACE_PATH;
  try {
    const cfg = await getConfig();
    if (cfg?.workspace_root) workspaceRoot = cfg.workspace_root.replace(/\\/g, '/');
  } catch {
    // Ignore and use default
  }
  return deleteWorkspaceFolder(workspaceRoot);
};

export const scanModelsFolder = async (
  customPathOrQueryContext?: string | QueryFunctionInvocation
): Promise<string[]> => {
  try {
    const customPath = isQueryFunctionInvocation(customPathOrQueryContext)
      ? undefined
      : customPathOrQueryContext;
    let modelsPath = customPath || DEFAULT_MODELS_PATH;
    if (!customPath) {
      try {
        const cfg = await getConfig();
        if (cfg?.default_models_path) {
          modelsPath = cfg.default_models_path.replace(/\\/g, '/');
        }
      } catch {
        // Ignore and use default
      }
    }

    if (!isTauriAvailable()) {
      return [];
    }

    const entries = await readDir(modelsPath);

    // Filter for .gguf files and return full paths
    const ggufFiles = entries
      .filter(entry => entry.name?.endsWith('.gguf'))
      .map(entry => `${modelsPath}/${entry.name}`);

    return ggufFiles;
  } catch (error) {
    console.error('[scanModelsFolder] Error scanning directory:', handleApiError(error));
    return [];
  }
};

export const copyModelToFolder = async (sourcePath: string): Promise<string> => {
  try {
    // Default models destination folder (config-aware)
    let modelsFolder = DEFAULT_MODELS_PATH;
    try {
      const cfg = await getConfig();
      if (cfg?.default_models_path) {
        modelsFolder = cfg.default_models_path.replace(/\\/g, '/');
      }
    } catch {
      // Ignore and use default
    }

    // Extract filename from source path
    const filename = sourcePath.split(/[/\\]/).pop();
    if (!filename) {
      throw new Error('Invalid source path');
    }

    // Create destination path
    const destPath = `${modelsFolder}/${filename}`;

    // Copy the file using Tauri filesystem API
    await copyFile(sourcePath, destPath);

    console.log(`[copyModelToFolder] Copied ${filename} to models folder`);
    return destPath;
  } catch (error) {
    console.error('[copyModelToFolder] Error copying file:', handleApiError(error));
    throw new Error(`Failed to copy model file: ${error}`);
  }
};

export const getConfig = async (): Promise<ConfigResponse> => {
  try {
    const { data } = await api.get('/config');
    return data;
  } catch (error) {
    // Return default config for CLI server mode
    return {
      status: 'offline',
      workspace_root: isWindows ? DEFAULT_WORKSPACE_PATH.replace(/\//g, '\\') : DEFAULT_WORKSPACE_PATH,
      port: 6930,
      host: '127.0.0.1',
      cors_enabled: true,
      timeout_seconds: 600,
      max_concurrent_requests: 8,
      default_models_path: isWindows ? DEFAULT_MODELS_PATH.replace(/\//g, '\\') : DEFAULT_MODELS_PATH,
      max_models: 10,
      default_ram_budget_mb: 16384,
      default_strategy: 'balanced',
      enable_gpu: true,
      features: {
        llm: true,
        diffusion: false,
        vision: false,
        video: false,
      },
    };
  }
};

export const getConfigRecommendations = async (): Promise<any> => {
  try {
    const { data } = await api.get('/config/recommendations');
    return data;
  } catch (error) {
    // Return default recommendations for CLI server mode
    return {
      status: 'success',
      recommended_ram_budget_mb: 16384,
      recommended_strategy: 'balanced',
      total_ram_gb: 32,
      max_concurrent_models: 4,
    };
  }
};

export const updateConfig = async (payload: ConfigUpdateRequest): Promise<ConfigUpdateResponse> => {
  const { data } = await api.post('/config', payload);
  return data;
};

// ============================================================================
// vPID L2 Context API (KV Cache Management)
// ============================================================================

export interface ContextIngestRequest {
  context_id?: string;
  model_id: string;
  content: string;
  metadata?: Record<string, unknown>;
}

export interface ContextIngestResponse {
  status: string;
  context_id: string;
  model_id: string;
  token_count: number;
  storage_size_mb: number;
  tier: 'hot' | 'warm' | 'cold';
  ingest_time_ms: number;
  message: string;
}

export interface Context {
  id: string;  // Mapped from context_id
  context_id?: string;  // Original from API
  model_id: string;
  name?: string;
  token_count: number;
  storage_size_mb?: number;
  memory_mb?: number;  // Alternative field name from API
  tier: 'hot' | 'warm' | 'cold';
  created_at?: string;
  last_accessed?: string;
  access_count?: number;
  status?: string;
  metadata?: Record<string, unknown>;
}

export interface ContextListResponse {
  status: string;
  contexts: Context[];
  count: number;
  total_size_mb: number;
  tier_distribution: {
    hot: number;
    warm: number;
    cold: number;
  };
}

export interface ContextQueryRequest {
  query: string;
  max_tokens?: number;
  temperature?: number;
  top_p?: number;
  top_k?: number;
  repeat_penalty?: number;
}

export interface ContextQueryResponse {
  status: string;
  context_id: string;
  response: string;
  cache_hit: boolean;
  usage: {
    context_tokens: number;
    query_tokens: number;
    generated_tokens: number;
    total_tokens: number;
  };
  latency_ms: number;
  total_time_ms: number;
  speedup: string;
}

export interface ContextStatsResponse {
  status: string;
  total_contexts: number;
  total_size_mb: number;
  cache_hits: number;
  cache_misses: number;
  hit_rate: number;
  avg_query_latency_ms: number;
  tier_stats: {
    hot: { count: number; size_mb: number };
    warm: { count: number; size_mb: number };
    cold: { count: number; size_mb: number };
  };
  memory_usage: {
    gpu_mb: number;
    cpu_mb: number;
    disk_mb: number;
  };
}

export interface ContextTierChangeResponse {
  status: string;
  message: string;
  current_tier: string;
}

// Ingest a context (pre-compute KV cache) - O(n^2) operation
export const ingestContext = async (request: ContextIngestRequest): Promise<ContextIngestResponse> => {
  const { data } = await api.post('/contexts/ingest', request, { timeout: 600000 }); // 10 min timeout for large docs
  return data;
};

// List all contexts
export const listContexts = async (tier?: string, model_id?: string): Promise<ContextListResponse> => {
  const params = new URLSearchParams();
  if (tier) params.append('tier', tier);
  if (model_id) params.append('model_id', model_id);
  const { data } = await api.get(`/contexts${params.toString() ? '?' + params.toString() : ''}`);

  // Map context_id to id for UI compatibility
  if (data.contexts) {
    data.contexts = data.contexts.map((ctx: any) => ({
      ...ctx,
      id: ctx.context_id || ctx.id,
      storage_size_mb: ctx.storage_size_mb || ctx.memory_mb,
    }));
  }

  return data;
};

// Get a specific context
export const getContext = async (contextId: string): Promise<Context> => {
  const { data } = await api.get(`/contexts/${encodeURIComponent(contextId)}`);
  return data.context;
};

// Query with cached context - O(1) lookup + O(q^2) for query only
export const queryContext = async (contextId: string, request: ContextQueryRequest): Promise<ContextQueryResponse> => {
  const { data } = await api.post(`/contexts/${encodeURIComponent(contextId)}/query`, request);
  return data;
};

// Delete a context
export const deleteContext = async (contextId: string): Promise<{ status: string; message: string }> => {
  const { data } = await api.delete(`/contexts/${encodeURIComponent(contextId)}`);
  return data;
};

// Promote context to hot tier (GPU)
export const promoteContext = async (contextId: string): Promise<ContextTierChangeResponse> => {
  const { data } = await api.post(`/contexts/${encodeURIComponent(contextId)}/promote`, { tier: 'hot' });
  return data;
};

// Demote context to cold tier (disk)
export const demoteContext = async (contextId: string): Promise<ContextTierChangeResponse> => {
  const { data } = await api.post(`/contexts/${encodeURIComponent(contextId)}/demote`, { tier: 'cold' });
  return data;
};

// Get context statistics
export const getContextStats = async (): Promise<ContextStatsResponse> => {
  const { data } = await api.get('/contexts/stats');
  // Transform API response to match frontend interface
  const stats = data.stats || data;
  const tiering = data.tiering_summary || {};
  return {
    status: data.status || 'success',
    total_contexts: stats.total_contexts || 0,
    total_size_mb: stats.total_memory_mb || 0,
    cache_hits: stats.cache_hits || 0,
    cache_misses: stats.cache_misses || 0,
    hit_rate: stats.hit_rate || 0,
    avg_query_latency_ms: stats.avg_query_latency_ms || 0,
    tier_stats: {
      hot: { count: tiering.hot_tier?.contexts || stats.hot_contexts || 0, size_mb: tiering.hot_tier?.memory_mb || stats.hot_memory_mb || 0 },
      warm: { count: tiering.warm_tier?.contexts || stats.warm_contexts || 0, size_mb: tiering.warm_tier?.memory_mb || stats.warm_memory_mb || 0 },
      cold: { count: tiering.cold_tier?.contexts || stats.cold_contexts || 0, size_mb: tiering.cold_tier?.memory_mb || stats.cold_memory_mb || 0 },
    },
    memory_usage: {
      gpu_mb: tiering.hot_tier?.memory_mb || stats.hot_memory_mb || 0,
      cpu_mb: tiering.warm_tier?.memory_mb || stats.warm_memory_mb || 0,
      disk_mb: tiering.cold_tier?.memory_mb || stats.cold_memory_mb || 0,
    },
  };
};

// ============================================================================
// Config
// ============================================================================

export interface ConfigFeatures {
  llm?: boolean;
  diffusion?: boolean;
  vision?: boolean;
  video?: boolean;
}

export interface ConfigResponse {
  status: string;
  host: string;
  port: number;
  workspace_root: string;
  default_models_path?: string;
  config_path?: string;
  cors_enabled?: boolean;
  timeout_seconds?: number;
  max_concurrent_requests?: number;
  max_active_inferences?: number;
  max_models?: number;
  default_ram_budget_mb?: number;
  default_strategy?: string;
  enable_gpu?: boolean;
  features?: ConfigFeatures;
}

export interface ConfigUpdateRequest {
  server?: {
    host?: string;
    port?: number;
    cors_enabled?: boolean;
    timeout_seconds?: number;
    max_concurrent_requests?: number;
    max_active_inferences?: number;
  };
  workspace?: {
    workspace_root?: string;
    default_models_path?: string;
  };
  runtime?: {
    max_models?: number;
    default_ram_budget_mb?: number;
    default_strategy?: string;
    enable_gpu?: boolean;
  };
  // Flat keys (supported by backend for convenience)
  host?: string;
  port?: number;
  cors_enabled?: boolean;
  timeout_seconds?: number;
  max_concurrent_requests?: number;
  max_active_inferences?: number;
  workspace_root?: string;
  default_models_path?: string;
  max_models?: number;
  default_ram_budget_mb?: number;
  default_strategy?: string;
  enable_gpu?: boolean;
}

export interface ConfigUpdateResponse {
  status: string;
  updated_fields: string[];
  restart_required: boolean;
  restart_required_fields: string[];
  config_path?: string;
}

export const getApiBaseUrl = (): string => API_BASE_URL;
export const getApiTransportBaseUrl = (): string => API_TRANSPORT_BASE_URL;
export const getApiVersion = (): string => 'v1';

export default api;
