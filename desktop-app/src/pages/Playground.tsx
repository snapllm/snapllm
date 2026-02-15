
// ============================================================================
// SnapLLM - API Playground
// ============================================================================

import React, { useEffect, useMemo, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { clsx } from 'clsx';
import { motion } from 'framer-motion';
import {
  AlertTriangle,
  BookOpen,
  Copy,
  Cpu,
  ExternalLink,
  Eye,
  FileJson,
  History,
  Image as ImageIcon,
  Info,
  Loader2,
  MessageSquare,
  Play,
  RefreshCw,
  Server,
  Terminal,
  Upload,
  X,
} from 'lucide-react';
import { listModels, getApiBaseUrl } from '../lib/api';
import { Badge, Button, Card, IconButton, Input, SearchInput, Tabs } from '../components/ui';
// ============================================================================
// Types
// ============================================================================

type EndpointCategory = 'system' | 'chat' | 'models' | 'image' | 'vision' | 'context';

type HttpMethod = 'GET' | 'POST' | 'PUT' | 'DELETE';

interface ApiEndpoint {
  id: string;
  name: string;
  method: HttpMethod;
  path: string;
  description: string;
  category: EndpointCategory;
}

interface RequestHistoryEntry {
  id: string;
  name: string;
  method: HttpMethod;
  path: string;
  queryString: string;
  status: number;
  durationMs: number;
  timestamp: Date;
  requestBody: string;
  responseBody: string;
}

// ============================================================================
// Endpoint Catalog
// ============================================================================

const API_ENDPOINTS: ApiEndpoint[] = [
  // System
  {
    id: 'health',
    name: 'Health Check',
    method: 'GET',
    path: '/health',
    description: 'Check server health and version',
    category: 'system',
  },
  {
    id: 'metrics',
    name: 'Server Metrics',
    method: 'GET',
    path: '/api/v1/server/metrics',
    description: 'Runtime metrics and server statistics',
    category: 'system',
  },
  {
    id: 'config',
    name: 'Get Config',
    method: 'GET',
    path: '/api/v1/config',
    description: 'Read current configuration',
    category: 'system',
  },
  {
    id: 'config-update',
    name: 'Update Config',
    method: 'POST',
    path: '/api/v1/config',
    description: 'Persist configuration changes',
    category: 'system',
  },
  {
    id: 'config-recommend',
    name: 'Config Recommendations',
    method: 'GET',
    path: '/api/v1/config/recommendations',
    description: 'Suggested runtime settings',
    category: 'system',
  },

  // Chat & Text
  {
    id: 'chat',
    name: 'Chat Completions',
    method: 'POST',
    path: '/v1/chat/completions',
    description: 'OpenAI chat format (SSE supported)',
    category: 'chat',
  },
  {
    id: 'messages',
    name: 'Messages (Anthropic)',
    method: 'POST',
    path: '/v1/messages',
    description: 'Anthropic Messages API compatible',
    category: 'chat',
  },
  {
    id: 'generate',
    name: 'Text Generation',
    method: 'POST',
    path: '/api/v1/generate',
    description: 'Prompt-based generation (native)',
    category: 'chat',
  },
  {
    id: 'generate-batch',
    name: 'Batch Generation',
    method: 'POST',
    path: '/api/v1/generate/batch',
    description: 'Parallel batch generation with per-prompt system prompts',
    category: 'chat',
  },

  // Models
  {
    id: 'models-list-openai',
    name: 'List Models (OpenAI)',
    method: 'GET',
    path: '/v1/models',
    description: 'OpenAI model listing format',
    category: 'models',
  },
  {
    id: 'models-list',
    name: 'List Models',
    method: 'GET',
    path: '/api/v1/models',
    description: 'Extended model listing with status',
    category: 'models',
  },
  {
    id: 'models-load',
    name: 'Load Model',
    method: 'POST',
    path: '/api/v1/models/load',
    description: 'Load a model into memory',
    category: 'models',
  },
  {
    id: 'models-switch',
    name: 'Switch Model',
    method: 'POST',
    path: '/api/v1/models/switch',
    description: 'Switch active model (<1ms)',
    category: 'models',
  },
  {
    id: 'models-unload',
    name: 'Unload Model',
    method: 'POST',
    path: '/api/v1/models/unload',
    description: 'Unload a model from memory',
    category: 'models',
  },
  {
    id: 'cache-stats',
    name: 'Cache Stats',
    method: 'GET',
    path: '/api/v1/models/cache/stats',
    description: 'vPID cache hit/miss statistics',
    category: 'models',
  },
  {
    id: 'cache-clear',
    name: 'Clear Cache',
    method: 'POST',
    path: '/api/v1/models/cache/clear',
    description: 'Clear vPID cache',
    category: 'models',
  },

  // Image (Diffusion)
  {
    id: 'image-gen',
    name: 'Generate Image',
    method: 'POST',
    path: '/api/v1/diffusion/generate',
    description: 'Stable Diffusion image generation',
    category: 'image',
  },

  // Vision / Multimodal
  {
    id: 'vision',
    name: 'Vision Analysis',
    method: 'POST',
    path: '/api/v1/vision/generate',
    description: 'Vision-language image understanding',
    category: 'vision',
  },

  // Context API (vPID L2)
  {
    id: 'contexts-ingest',
    name: 'Context Ingest',
    method: 'POST',
    path: '/api/v1/contexts/ingest',
    description: 'Pre-compute KV cache (O(n^2))',
    category: 'context',
  },
  {
    id: 'contexts-list',
    name: 'List Contexts',
    method: 'GET',
    path: '/api/v1/contexts',
    description: 'List cached contexts by tier or model',
    category: 'context',
  },
  {
    id: 'contexts-stats',
    name: 'Context Stats',
    method: 'GET',
    path: '/api/v1/contexts/stats',
    description: 'Context cache summary and tiers',
    category: 'context',
  },
  {
    id: 'contexts-query',
    name: 'Context Query',
    method: 'POST',
    path: '/api/v1/contexts/{context_id}/query',
    description: 'Query with cached context (O(1))',
    category: 'context',
  },
  {
    id: 'contexts-get',
    name: 'Get Context',
    method: 'GET',
    path: '/api/v1/contexts/{context_id}',
    description: 'Fetch context metadata',
    category: 'context',
  },
  {
    id: 'contexts-delete',
    name: 'Delete Context',
    method: 'DELETE',
    path: '/api/v1/contexts/{context_id}',
    description: 'Remove a cached context',
    category: 'context',
  },
  {
    id: 'contexts-promote',
    name: 'Promote Context',
    method: 'POST',
    path: '/api/v1/contexts/{context_id}/promote',
    description: 'Promote context to hot tier',
    category: 'context',
  },
  {
    id: 'contexts-demote',
    name: 'Demote Context',
    method: 'POST',
    path: '/api/v1/contexts/{context_id}/demote',
    description: 'Demote context to cold tier',
    category: 'context',
  },
];

const CATEGORY_META: Record<EndpointCategory, { label: string; icon: React.ComponentType<{ className?: string }> }> = {
  system: { label: 'System', icon: Server },
  chat: { label: 'Chat', icon: MessageSquare },
  models: { label: 'Models', icon: Cpu },
  image: { label: 'Image', icon: ImageIcon },
  vision: { label: 'Vision', icon: Eye },
  context: { label: 'Context', icon: FileJson },
};

const DEFAULT_MODEL_PATH = 'C:/Models/your-model.gguf';

const buildTemplate = (endpointId: string, modelHint: string) => {
  switch (endpointId) {
    case 'chat':
      return {
        model: modelHint,
        messages: [
          { role: 'user', content: 'Hello from SnapLLM.' },
        ],
        max_tokens: 256,
        temperature: 0.7,
        stream: false,
        use_context_cache: true,
      };
    case 'messages':
      return {
        model: modelHint,
        max_tokens: 512,
        temperature: 1.0,
        system: 'You are a helpful assistant.',
        messages: [
          { role: 'user', content: 'Hello from SnapLLM.' },
        ],
      };
    case 'generate':
      return {
        prompt: 'Write a short welcome message for a local AI server.',
        max_tokens: 256,
        temperature: 0.7,
        top_p: 0.95,
        top_k: 40,
        repeat_penalty: 1.1,
      };
    case 'generate-batch':
      return {
        items: [
          {
            messages: [
              { role: 'system', content: 'You are a concise summarizer. Return 2-3 sentences max.' },
              { role: 'user', content: 'Summarize the key benefits of local LLM inference.' },
            ],
            max_tokens: 128,
            temperature: 0.3,
          },
          {
            messages: [
              { role: 'system', content: 'You are a friendly copywriter.' },
              { role: 'user', content: 'Write a short onboarding message for new users.' },
            ],
            max_tokens: 256,
            temperature: 0.8,
          },
        ],
        model: modelHint,
      };
    case 'image-gen':
      return {
        prompt: 'A cinematic skyline at sunrise, ultra-detailed, 4k.',
        negative_prompt: 'blurry, low quality, artifacts',
        model: modelHint,
        width: 512,
        height: 512,
        steps: 20,
        cfg_scale: 7.0,
        seed: -1,
      };
    case 'vision':
      return {
        model: modelHint,
        images: ['<base64-image-bytes>'],
        prompt: 'Describe the image and list any visible objects.',
        max_tokens: 256,
        temperature: 0.7,
        top_p: 0.95,
        top_k: 40,
        repeat_penalty: 1.1,
      };
    case 'models-load':
      return {
        model_id: 'my-model',
        file_path: DEFAULT_MODEL_PATH,
        model_type: 'auto',
        strategy: 'balanced',
      };
    case 'models-switch':
      return {
        model_id: modelHint,
      };
    case 'models-unload':
      return {
        model_id: modelHint,
      };
    case 'cache-clear':
      return {};
    case 'config-update':
      return {
        host: '127.0.0.1',
        port: 6930,
        cors_enabled: true,
        timeout_seconds: 600,
        max_concurrent_requests: 8,
        workspace_root: 'C:/SnapLLM_Workspace',
        default_models_path: 'C:/Models',
        max_models: 10,
        default_ram_budget_mb: 16384,
        default_strategy: 'balanced',
        enable_gpu: true,
      };
    case 'contexts-ingest':
      return {
        model_id: modelHint,
        content: 'Paste reference text here to pre-compute a KV cache.',
        name: 'reference-doc',
        ttl_seconds: 86400,
        priority: 'normal',
        dtype: 'fp16',
        compress: false,
      };
    case 'contexts-query':
      return {
        query: 'Summarize the cached document in three bullets.',
        max_tokens: 512,
        temperature: 0.7,
        top_p: 0.95,
        top_k: 40,
        repeat_penalty: 1.1,
      };
    case 'contexts-promote':
      return { tier: 'hot' };
    case 'contexts-demote':
      return { tier: 'cold' };
    default:
      return null;
  }
};

const safeParseJson = (value: string) => {
  try {
    return { ok: true, data: JSON.parse(value) } as const;
  } catch (error: any) {
    return { ok: false, error: error?.message || 'Invalid JSON' } as const;
  }
};

const isMaybeBase64 = (value: string) => {
  if (!value || value.length < 80) return false;
  return /^[A-Za-z0-9+/=]+$/.test(value);
};

const extractImageUrls = (payload: any, apiBaseUrl: string): string[] => {
  if (!payload || typeof payload !== 'object') return [];

  let urls: string[] = [];

  if (Array.isArray(payload.images)) {
    urls = payload.images.filter((item: any) => typeof item === 'string');
  } else if (typeof payload.image_url === 'string') {
    urls = [payload.image_url];
  } else if (typeof payload.image === 'string') {
    urls = [payload.image];
  }

  return urls
    .map((url) => {
      if (!url) return '';
      if (url.startsWith('http://') || url.startsWith('https://') || url.startsWith('data:')) {
        return url;
      }
      if (isMaybeBase64(url)) {
        return `data:image/png;base64,${url}`;
      }
      if (url.startsWith('/')) {
        return `${apiBaseUrl}${url}`;
      }
      return `${apiBaseUrl}/${url}`;
    })
    .filter(Boolean);
};

// ============================================================================
// Component
// ============================================================================

export default function Playground() {
  const apiBaseUrl = getApiBaseUrl();

  const [selectedEndpoint, setSelectedEndpoint] = useState<ApiEndpoint>(API_ENDPOINTS[0]);
  const [pathOverride, setPathOverride] = useState(API_ENDPOINTS[0].path);
  const [queryString, setQueryString] = useState('');
  const [requestBody, setRequestBody] = useState('');
  const [requestError, setRequestError] = useState<string | null>(null);
  const [responseBody, setResponseBody] = useState('');
  const [responseJson, setResponseJson] = useState<any | null>(null);
  const [responseError, setResponseError] = useState<string | null>(null);
  const [responseStatus, setResponseStatus] = useState<number | null>(null);
  const [responseDurationMs, setResponseDurationMs] = useState<number | null>(null);
  const [history, setHistory] = useState<RequestHistoryEntry[]>([]);
  const [searchTerm, setSearchTerm] = useState('');
  const [activeCategory, setActiveCategory] = useState<EndpointCategory | 'all'>('all');
  const [activeSnippet, setActiveSnippet] = useState<'curl' | 'python' | 'javascript'>('curl');
  const [showNotice, setShowNotice] = useState(true);
  const [isLoading, setIsLoading] = useState(false);
  const [imagePreview, setImagePreview] = useState<string | null>(null);

  const requestDraftsRef = useRef<Record<string, string>>({});
  const skipResetRef = useRef(false);
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  const { data: modelsResponse, isFetching, isError } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
    retry: 1,
  });

  const models = modelsResponse?.models || [];
  const loadedModels = models.filter((model: any) => model.status === 'loaded' || model.active);
  const defaultModel = loadedModels[0]?.id || loadedModels[0]?.name || models[0]?.id || models[0]?.name || 'default';

  const requestTemplate = useMemo(() => buildTemplate(selectedEndpoint.id, defaultModel), [selectedEndpoint.id, defaultModel]);

  useEffect(() => {
    if (skipResetRef.current) {
      skipResetRef.current = false;
      return;
    }

    const draft = requestDraftsRef.current[selectedEndpoint.id];
    const nextBody = draft ?? (requestTemplate ? JSON.stringify(requestTemplate, null, 2) : '');

    setPathOverride(selectedEndpoint.path);
    setQueryString('');
    setRequestError(null);
    setResponseError(null);
    setResponseStatus(null);
    setResponseDurationMs(null);
    setResponseBody('');
    setResponseJson(null);
    setImagePreview(null);
    setRequestBody(nextBody);
    requestDraftsRef.current[selectedEndpoint.id] = nextBody;
  }, [selectedEndpoint.id, requestTemplate]);

  const supportsBody = selectedEndpoint.method !== 'GET';
  const isVisionEndpoint = selectedEndpoint.category === 'vision';

  const groupedEndpoints = useMemo(() => {
    const filtered = API_ENDPOINTS.filter((endpoint) => {
      const matchesCategory = activeCategory === 'all' || endpoint.category === activeCategory;
      const term = searchTerm.trim().toLowerCase();
      const matchesSearch = !term
        || endpoint.name.toLowerCase().includes(term)
        || endpoint.path.toLowerCase().includes(term)
        || endpoint.description.toLowerCase().includes(term);
      return matchesCategory && matchesSearch;
    });

    const grouped: Record<EndpointCategory, ApiEndpoint[]> = {
      system: [],
      chat: [],
      models: [],
      image: [],
      vision: [],
      context: [],
    };

    filtered.forEach((endpoint) => {
      grouped[endpoint.category].push(endpoint);
    });

    return grouped;
  }, [activeCategory, searchTerm]);

  const categoryCounts = useMemo(() => {
    const counts: Record<EndpointCategory, number> = {
      system: 0,
      chat: 0,
      models: 0,
      image: 0,
      vision: 0,
      context: 0,
    };

    API_ENDPOINTS.forEach((endpoint) => {
      counts[endpoint.category] += 1;
    });

    return counts;
  }, []);

  const connectionStatus = isError ? 'offline' : isFetching ? 'checking' : 'online';

  const fullUrl = useMemo(() => {
    const trimmedPath = (pathOverride || selectedEndpoint.path).trim();
    const normalizedPath = trimmedPath.startsWith('/') ? trimmedPath : `/${trimmedPath}`;
    const rawQuery = queryString.trim();
    const normalizedQuery = rawQuery ? (rawQuery.startsWith('?') ? rawQuery.slice(1) : rawQuery) : '';
    return `${apiBaseUrl}${normalizedPath}${normalizedQuery ? `?${normalizedQuery}` : ''}`;
  }, [apiBaseUrl, pathOverride, queryString, selectedEndpoint.path]);

  const pathNeedsReplacement = /\{[^}]+\}|:\w+/.test(pathOverride);

  const responseImages = useMemo(() => extractImageUrls(responseJson, apiBaseUrl), [responseJson, apiBaseUrl]);

  const updateRequestBody = (value: string) => {
    setRequestBody(value);
    requestDraftsRef.current[selectedEndpoint.id] = value;
    if (requestError) setRequestError(null);
  };

  const prettifyRequestBody = () => {
    if (!requestBody.trim()) return;
    const parsed = safeParseJson(requestBody);
    if (!parsed.ok) {
      setRequestError(`Invalid JSON: ${parsed.error}`);
      return;
    }
    updateRequestBody(JSON.stringify(parsed.data, null, 2));
  };

  const resetRequestBody = () => {
    const nextBody = requestTemplate ? JSON.stringify(requestTemplate, null, 2) : '';
    updateRequestBody(nextBody);
  };

  const copyToClipboard = async (value: string) => {
    try {
      await navigator.clipboard.writeText(value);
    } catch {
      // Ignore clipboard failures
    }
  };

  const handleSendRequest = async () => {
    setRequestError(null);
    setResponseError(null);

    let bodyPayload: string | undefined = undefined;

    if (supportsBody) {
      const parsed = safeParseJson(requestBody || '{}');
      if (!parsed.ok) {
        setRequestError(`Invalid JSON: ${parsed.error}`);
        return;
      }
      bodyPayload = JSON.stringify(parsed.data);
    }

    setIsLoading(true);
    const startTime = performance.now();

    try {
      const response = await fetch(fullUrl, {
        method: selectedEndpoint.method,
        headers: {
          'Content-Type': 'application/json',
        },
        body: supportsBody ? bodyPayload : undefined,
      });

      const durationMs = performance.now() - startTime;
      const rawText = await response.text();

      let parsedJson: any = null;
      try {
        parsedJson = rawText ? JSON.parse(rawText) : null;
      } catch {
        parsedJson = null;
      }

      const formatted = parsedJson ? JSON.stringify(parsedJson, null, 2) : rawText;

      setResponseBody(formatted || '');
      setResponseJson(parsedJson);
      setResponseStatus(response.status);
      setResponseDurationMs(durationMs);

      if (!response.ok) {
        const errorMessage = parsedJson?.error?.message || response.statusText || 'Request failed';
        setResponseError(errorMessage);
      }

      setHistory((prev) => [
        {
          id: crypto.randomUUID(),
          name: selectedEndpoint.name,
          method: selectedEndpoint.method,
          path: pathOverride,
          queryString,
          status: response.status,
          durationMs,
          timestamp: new Date(),
          requestBody,
          responseBody: formatted || rawText,
        },
        ...prev,
      ].slice(0, 30));
    } catch (error: any) {
      const durationMs = performance.now() - startTime;
      const message = error?.message || 'Network error';
      const fallbackResponse = JSON.stringify({ error: message }, null, 2);

      setResponseBody(fallbackResponse);
      setResponseJson({ error: message });
      setResponseStatus(0);
      setResponseDurationMs(durationMs);
      setResponseError(message);

      setHistory((prev) => [
        {
          id: crypto.randomUUID(),
          name: selectedEndpoint.name,
          method: selectedEndpoint.method,
          path: pathOverride,
          queryString,
          status: 0,
          durationMs,
          timestamp: new Date(),
          requestBody,
          responseBody: fallbackResponse,
        },
        ...prev,
      ].slice(0, 30));
    } finally {
      setIsLoading(false);
    }
  };

  const handleHistorySelect = (entry: RequestHistoryEntry) => {
    const endpointMatch = API_ENDPOINTS.find((endpoint) => endpoint.method === entry.method && endpoint.path === entry.path);
    const shouldSkipReset = endpointMatch && endpointMatch.id !== selectedEndpoint.id;
    skipResetRef.current = Boolean(shouldSkipReset);
    if (endpointMatch && endpointMatch.id !== selectedEndpoint.id) {
      setSelectedEndpoint(endpointMatch);
    }
    setPathOverride(entry.path);
    setQueryString(entry.queryString);
    setRequestBody(entry.requestBody);
    setResponseBody(entry.responseBody);
    setResponseStatus(entry.status || null);
    setResponseDurationMs(entry.durationMs || null);
    setResponseError(null);
    requestDraftsRef.current[endpointMatch?.id || selectedEndpoint.id] = entry.requestBody;

    try {
      setResponseJson(JSON.parse(entry.responseBody));
    } catch {
      setResponseJson(null);
    }
  };

  const handleImageUpload = async (file: File) => {
    const reader = new FileReader();

    reader.onload = () => {
      const result = reader.result;
      if (typeof result !== 'string') return;

      const [, base64] = result.split(',');
      if (!base64) return;

      setImagePreview(result);

      const parsed = safeParseJson(requestBody || '{}');
      const body = parsed.ok && parsed.data && typeof parsed.data === 'object'
        ? { ...parsed.data }
        : {};

      body.images = [base64];
      if (!body.prompt) {
        body.prompt = 'Describe the image in detail.';
      }

      updateRequestBody(JSON.stringify(body, null, 2));
    };

    reader.readAsDataURL(file);
  };

  const generateSnippet = (language: 'curl' | 'python' | 'javascript') => {
    const parsed = supportsBody ? safeParseJson(requestBody || '{}') : null;
    const bodyPayload = parsed?.ok ? JSON.stringify(parsed.data) : (requestBody || '{}');

    if (language === 'curl') {
      if (!supportsBody) {
        return `curl "${fullUrl}"`;
      }
      const escaped = bodyPayload.replace(/'/g, "\\'");
      return `curl -X ${selectedEndpoint.method} \\
  "${fullUrl}" \\
  -H "Content-Type: application/json" \\
  -d '${escaped}'`;
    }

    if (language === 'python') {
      if (!supportsBody) {
        return `import requests\n\nresponse = requests.get("${fullUrl}")\nprint(response.json())`;
      }
      return `import requests\n\nresponse = requests.${selectedEndpoint.method.toLowerCase()}(\n    "${fullUrl}",\n    json=${bodyPayload}\n)\nprint(response.json())`;
    }

    if (!supportsBody) {
      return `fetch("${fullUrl}")\n  .then(res => res.json())\n  .then(console.log);`;
    }

    return `fetch("${fullUrl}", {\n  method: "${selectedEndpoint.method}",\n  headers: { "Content-Type": "application/json" },\n  body: JSON.stringify(${bodyPayload})\n})\n  .then(res => res.json())\n  .then(console.log);`;
  };

  const snippetTabs = [
    { id: 'curl', label: 'cURL' },
    { id: 'python', label: 'Python' },
    { id: 'javascript', label: 'JavaScript' },
  ];

  return (
    <div className="flex flex-col h-[calc(100vh-4rem)] overflow-hidden -m-6">
      {showNotice && (
        <motion.div
          initial={{ opacity: 0, y: -8 }}
          animate={{ opacity: 1, y: 0 }}
          className="border-b border-amber-200/80 dark:border-amber-900/50 bg-gradient-to-r from-amber-50 via-orange-50 to-amber-50 dark:from-amber-900/20 dark:via-orange-900/10 dark:to-amber-900/20"
        >
          <div className="px-6 py-3 flex items-start gap-3">
            <div className="w-9 h-9 rounded-xl bg-amber-500/15 text-amber-700 dark:text-amber-300 flex items-center justify-center">
              <Info className="w-4 h-4" />
            </div>
            <div className="flex-1 min-w-0">
              <div className="flex items-center gap-2">
                <h3 className="text-sm font-semibold text-amber-900 dark:text-amber-100">
                  API Playground
                </h3>
                <Badge variant="warning" size="sm">Local Mode</Badge>
              </div>
              <p className="text-xs text-amber-700/80 dark:text-amber-200/80 mt-1">
                This workspace is optimized for local testing. Use the endpoints below to validate models, pipelines, and vPID cache behavior without external auth.
              </p>
              <div className="flex items-center gap-4 mt-2">
                <button
                  onClick={() => copyToClipboard('snapllm.exe --server --port 6930')}
                  className="text-xs text-amber-700 dark:text-amber-200 hover:underline flex items-center gap-1"
                >
                  <Terminal className="w-3 h-3" />
                  Copy CLI startup
                </button>
                <button
                  onClick={() => window.open(`${apiBaseUrl}/health`, '_blank')}
                  className="text-xs text-amber-700 dark:text-amber-200 hover:underline flex items-center gap-1"
                >
                  <ExternalLink className="w-3 h-3" />
                  Open health endpoint
                </button>
              </div>
            </div>
            <button
              onClick={() => setShowNotice(false)}
              className="p-1 rounded hover:bg-amber-500/10 text-amber-700 dark:text-amber-200"
              aria-label="Dismiss"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        </motion.div>
      )}

      <div className="flex flex-1 overflow-hidden">
        {/* Left Panel: Endpoint Explorer */}
        <div className="w-80 border-r border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900 flex flex-col">
          <div className="p-4 border-b border-surface-200 dark:border-surface-800">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="text-sm font-semibold text-surface-900 dark:text-white">API Explorer</h2>
                <p className="text-xs text-surface-500">Base URL: {apiBaseUrl}</p>
              </div>
              <Badge
                variant={
                  connectionStatus === 'online'
                    ? 'success'
                    : connectionStatus === 'checking'
                    ? 'warning'
                    : 'error'
                }
                size="sm"
              >
                {connectionStatus === 'checking' ? 'Checking' : connectionStatus === 'online' ? 'Online' : 'Offline'}
              </Badge>
            </div>

            <div className="mt-4">
              <SearchInput
                value={searchTerm}
                onChange={(event) => setSearchTerm(event.target.value)}
                onClear={() => setSearchTerm('')}
                placeholder="Search endpoints..."
              />
            </div>

            <div className="mt-4 flex flex-wrap gap-2">
              {(['all', 'system', 'chat', 'models', 'image', 'vision', 'context'] as const).map((category) => {
                const isActive = activeCategory === category;
                const count = category === 'all'
                  ? API_ENDPOINTS.length
                  : categoryCounts[category];

                return (
                  <button
                    key={category}
                    onClick={() => setActiveCategory(category)}
                    className={clsx(
                      'px-2.5 py-1 rounded-full text-xs font-medium border transition-colors',
                      isActive
                        ? 'bg-brand-600 text-white border-brand-600'
                        : 'bg-surface-50 dark:bg-surface-800 text-surface-600 dark:text-surface-300 border-surface-200 dark:border-surface-700 hover:bg-surface-100 dark:hover:bg-surface-700'
                    )}
                  >
                    {category === 'all' ? 'All' : CATEGORY_META[category].label}
                    <span className="ml-1 text-[10px] opacity-80">{count}</span>
                  </button>
                );
              })}
            </div>
          </div>

          <div className="flex-1 overflow-y-auto p-2">
            {Object.entries(groupedEndpoints).map(([category, endpoints]) => {
              if (endpoints.length === 0) return null;
              const meta = CATEGORY_META[category as EndpointCategory];
              const Icon = meta.icon;

              return (
                <div key={category} className="mb-4">
                  <div className="flex items-center gap-2 px-3 py-2 text-xs font-semibold text-surface-500 uppercase">
                    <Icon className="w-4 h-4" />
                    {meta.label}
                  </div>
                  <div className="space-y-1">
                    {endpoints.map((endpoint) => (
                      <button
                        key={endpoint.id}
                        onClick={() => setSelectedEndpoint(endpoint)}
                        className={clsx(
                          'w-full flex items-center gap-2 px-3 py-2 rounded-lg text-left transition-colors',
                          selectedEndpoint.id === endpoint.id
                            ? 'bg-brand-50 dark:bg-brand-900/20 text-brand-700 dark:text-brand-300'
                            : 'hover:bg-surface-100 dark:hover:bg-surface-800'
                        )}
                      >
                        <Badge
                          variant={
                            endpoint.method === 'GET'
                              ? 'success'
                              : endpoint.method === 'POST'
                              ? 'warning'
                              : endpoint.method === 'DELETE'
                              ? 'error'
                              : 'info'
                          }
                          size="sm"
                        >
                          {endpoint.method}
                        </Badge>
                        <div className="flex-1 min-w-0">
                          <p className="text-sm font-medium text-surface-800 dark:text-surface-100 truncate">
                            {endpoint.name}
                          </p>
                          <p className="text-xs text-surface-500 truncate">{endpoint.path}</p>
                        </div>
                      </button>
                    ))}
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Main Panel */}
        <div className="flex-1 flex flex-col min-w-0">
          <div className="px-6 py-4 border-b border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900">
            <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
              <div className="flex items-start gap-3">
                <div className="w-11 h-11 rounded-xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
                  <Play className="w-5 h-5 text-white" />
                </div>
                <div>
                  <div className="flex items-center gap-2">
                    <h2 className="text-lg font-semibold text-surface-900 dark:text-white">{selectedEndpoint.name}</h2>
                    <Badge variant="default" size="sm">{selectedEndpoint.category.toUpperCase()}</Badge>
                  </div>
                  <p className="text-sm text-surface-500 mt-1">{selectedEndpoint.description}</p>
                </div>
              </div>

              <div className="flex items-center gap-2">
                <Button
                  variant="secondary"
                  size="sm"
                  onClick={() => window.open(`${apiBaseUrl}/docs`, '_blank')}
                >
                  <BookOpen className="w-4 h-4" />
                  Docs
                </Button>
                <Button
                  variant="primary"
                  size="sm"
                  onClick={handleSendRequest}
                  disabled={isLoading}
                >
                  {isLoading ? <Loader2 className="w-4 h-4 animate-spin" /> : <Play className="w-4 h-4" />}
                  Send Request
                </Button>
              </div>
            </div>
          </div>

          <div className="flex-1 overflow-y-auto bg-surface-50 dark:bg-surface-950">
            <div className="p-6 space-y-6">
              {/* Endpoint Details */}
              <Card className="p-5">
                <div className="flex flex-col gap-4 lg:flex-row lg:items-end">
                  <div className="flex-1">
                    <label className="label">Endpoint Path</label>
                    <div className="flex items-center gap-2">
                      <Badge
                        variant={selectedEndpoint.method === 'GET' ? 'success' : 'warning'}
                        size="sm"
                      >
                        {selectedEndpoint.method}
                      </Badge>
                      <Input
                        value={pathOverride}
                        onChange={(event) => setPathOverride(event.target.value)}
                        className="font-mono text-sm"
                      />
                    </div>
                  </div>

                  <div className="flex-1">
                    <label className="label">Query String</label>
                    <Input
                      value={queryString}
                      onChange={(event) => setQueryString(event.target.value)}
                      placeholder="type=llm"
                      className="font-mono text-sm"
                    />
                  </div>

                  <div className="flex items-center gap-2">
                    <Button
                      variant="secondary"
                      size="sm"
                      onClick={() => copyToClipboard(fullUrl)}
                    >
                      <Copy className="w-4 h-4" />
                      Copy URL
                    </Button>
                    <Button
                      variant="secondary"
                      size="sm"
                      onClick={() => window.open(fullUrl, '_blank')}
                    >
                      <ExternalLink className="w-4 h-4" />
                      Open
                    </Button>
                  </div>
                </div>

                <div className="mt-3 text-xs text-surface-500">
                  Full URL:
                  <code className="ml-2 px-2 py-1 rounded bg-surface-100 dark:bg-surface-800 text-surface-700 dark:text-surface-300">
                    {fullUrl}
                  </code>
                </div>

                {pathNeedsReplacement && (
                  <div className="mt-3 flex items-start gap-2 text-xs text-warning-700 dark:text-warning-300 bg-warning-50 dark:bg-warning-900/20 border border-warning-200 dark:border-warning-800 rounded-lg p-2">
                    <AlertTriangle className="w-4 h-4 mt-0.5" />
                    Replace path parameters like {`{context_id}`} before sending the request.
                  </div>
                )}
              </Card>

              {/* Request and Response */}
              <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
                <Card className="p-5 flex flex-col min-h-[360px]">
                  <div className="flex items-center justify-between mb-3">
                    <div>
                      <h3 className="text-sm font-semibold text-surface-900 dark:text-white">Request Body</h3>
                      <p className="text-xs text-surface-500">JSON payload for the request</p>
                    </div>
                    <div className="flex items-center gap-2">
                      <Button variant="secondary" size="xs" onClick={resetRequestBody}>
                        <RefreshCw className="w-3.5 h-3.5" />
                        Reset
                      </Button>
                      <Button variant="secondary" size="xs" onClick={prettifyRequestBody}>
                        <FileJson className="w-3.5 h-3.5" />
                        Format
                      </Button>
                      <IconButton
                        icon={<Copy className="w-4 h-4" />}
                        label="Copy Request"
                        size="sm"
                        onClick={() => copyToClipboard(requestBody)}
                      />
                    </div>
                  </div>

                  {supportsBody ? (
                    <textarea
                      value={requestBody}
                      onChange={(event) => updateRequestBody(event.target.value)}
                      className="flex-1 w-full rounded-xl border border-surface-200 dark:border-surface-800 bg-surface-950 text-green-300 font-mono text-xs p-4 resize-none focus:outline-none"
                      spellCheck={false}
                    />
                  ) : (
                    <div className="flex-1 flex items-center justify-center text-sm text-surface-500 border border-dashed border-surface-300 dark:border-surface-700 rounded-xl">
                      This endpoint does not require a request body.
                    </div>
                  )}

                  {requestError && (
                    <div className="mt-2 text-xs text-error-600 dark:text-error-400 flex items-center gap-2">
                      <AlertTriangle className="w-4 h-4" />
                      {requestError}
                    </div>
                  )}

                  {isVisionEndpoint && (
                    <div className="mt-4 rounded-xl border border-purple-200 dark:border-purple-800 bg-purple-50/60 dark:bg-purple-900/20 p-3">
                      <div className="flex items-center justify-between">
                        <div>
                          <p className="text-sm font-medium text-purple-900 dark:text-purple-100">Image Input</p>
                          <p className="text-xs text-purple-700 dark:text-purple-300">Upload an image to auto-fill base64 in the request.</p>
                        </div>
                        <Button
                          variant="secondary"
                          size="xs"
                          onClick={() => fileInputRef.current?.click()}
                        >
                          <Upload className="w-3.5 h-3.5" />
                          Upload
                        </Button>
                      </div>
                      <input
                        ref={fileInputRef}
                        type="file"
                        accept="image/*"
                        className="hidden"
                        onChange={(event) => {
                          const file = event.target.files?.[0];
                          if (file) handleImageUpload(file);
                          event.target.value = '';
                        }}
                      />
                      {imagePreview && (
                        <div className="mt-3">
                          <img
                            src={imagePreview}
                            alt="Vision preview"
                            className="rounded-lg border border-purple-200 dark:border-purple-800 max-h-48"
                          />
                        </div>
                      )}
                    </div>
                  )}
                </Card>

                <Card className="p-5 flex flex-col min-h-[360px]">
                  <div className="flex items-center justify-between mb-3">
                    <div>
                      <h3 className="text-sm font-semibold text-surface-900 dark:text-white">Response</h3>
                      <div className="flex items-center gap-2 text-xs text-surface-500 mt-1">
                        {responseStatus !== null && (
                          <Badge
                            variant={responseStatus >= 200 && responseStatus < 300 ? 'success' : responseStatus === 0 ? 'warning' : 'error'}
                            size="sm"
                          >
                            {responseStatus === 0 ? 'No Response' : responseStatus}
                          </Badge>
                        )}
                        {responseDurationMs !== null && (
                          <span>{responseDurationMs.toFixed(0)} ms</span>
                        )}
                        {responseBody && (
                          <span>{responseBody.length.toLocaleString()} chars</span>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center gap-2">
                      <IconButton
                        icon={<Copy className="w-4 h-4" />}
                        label="Copy Response"
                        size="sm"
                        onClick={() => copyToClipboard(responseBody)}
                      />
                    </div>
                  </div>

                  {responseError && (
                    <div className="mb-3 rounded-lg border border-error-200 dark:border-error-800 bg-error-50 dark:bg-error-900/20 p-3 text-xs text-error-700 dark:text-error-200">
                      {responseError}
                    </div>
                  )}

                  <div className="flex-1 overflow-auto rounded-xl border border-surface-200 dark:border-surface-800 bg-surface-950 text-surface-100 font-mono text-xs p-4 whitespace-pre-wrap">
                    {responseBody || 'Response will appear here after sending a request.'}
                  </div>

                  {responseImages.length > 0 && (
                    <div className="mt-4 grid grid-cols-1 md:grid-cols-2 gap-3">
                      {responseImages.map((url, index) => (
                        <div key={`${url}-${index}`} className="rounded-lg overflow-hidden border border-surface-200 dark:border-surface-800">
                          <img src={url} alt={`Generated ${index + 1}`} className="w-full h-full object-cover" />
                        </div>
                      ))}
                    </div>
                  )}
                </Card>
              </div>

              {/* Code Snippets */}
              <Card className="p-5">
                <div className="flex items-center justify-between mb-4">
                  <div>
                    <h3 className="text-sm font-semibold text-surface-900 dark:text-white">Code Snippet</h3>
                    <p className="text-xs text-surface-500">Copy request code for scripts or CLI use</p>
                  </div>
                  <Button
                    variant="secondary"
                    size="xs"
                    onClick={() => copyToClipboard(generateSnippet(activeSnippet))}
                  >
                    <Copy className="w-3.5 h-3.5" />
                    Copy
                  </Button>
                </div>

                <Tabs
                  tabs={snippetTabs}
                  activeTab={activeSnippet}
                  onChange={(tabId) => setActiveSnippet(tabId as 'curl' | 'python' | 'javascript')}
                  variant="pills"
                />

                <pre className="mt-3 rounded-lg bg-surface-950 text-surface-200 text-xs font-mono p-4 overflow-auto">
                  {generateSnippet(activeSnippet)}
                </pre>
              </Card>
            </div>
          </div>
        </div>

        {/* Right Panel: History */}
        <div className="w-72 border-l border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900 flex flex-col">
          <div className="p-4 border-b border-surface-200 dark:border-surface-800 flex items-center justify-between">
            <h3 className="text-sm font-semibold text-surface-900 dark:text-white flex items-center gap-2">
              <History className="w-4 h-4" />
              History
            </h3>
            <Button
              variant="secondary"
              size="xs"
              onClick={() => setHistory([])}
            >
              Clear
            </Button>
          </div>
          <div className="flex-1 overflow-y-auto p-3 space-y-2">
            {history.length === 0 && (
              <div className="text-xs text-surface-500 text-center py-6">
                No requests yet.
              </div>
            )}
            {history.map((entry) => (
              <button
                key={entry.id}
                onClick={() => handleHistorySelect(entry)}
                className="w-full text-left p-3 rounded-lg border border-surface-200 dark:border-surface-800 hover:bg-surface-50 dark:hover:bg-surface-800/60 transition-colors"
              >
                <div className="flex items-center gap-2 mb-1">
                  <Badge
                    variant={entry.status >= 200 && entry.status < 300 ? 'success' : entry.status === 0 ? 'warning' : 'error'}
                    size="sm"
                  >
                    {entry.status === 0 ? 'ERR' : entry.status}
                  </Badge>
                  <span className="text-xs text-surface-500">{entry.durationMs.toFixed(0)} ms</span>
                </div>
                <p className="text-sm font-medium text-surface-800 dark:text-surface-100 truncate">
                  {entry.name}
                </p>
                <p className="text-xs text-surface-500 truncate">{entry.path}</p>
                <p className="text-[10px] text-surface-400 mt-1">
                  {entry.timestamp.toLocaleTimeString()}
                </p>
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
