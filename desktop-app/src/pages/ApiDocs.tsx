import { useState, useMemo } from 'react';
import { Copy, CheckCircle, Server, Book } from 'lucide-react';
import { getApiBaseUrl } from '../lib/api';

interface EndpointExample {
  method: string;
  path: string;
  description: string;
  requestBody?: string;
  responseBody?: string;
  curlExample: string;
}

export default function ApiDocs() {
  const serverUrl = useMemo(() => getApiBaseUrl(), []);
  const [copiedId, setCopiedId] = useState<string | null>(null);

  const copyToClipboard = (text: string, id: string) => {
    navigator.clipboard.writeText(text);
    setCopiedId(id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  const endpoints: EndpointExample[] = [
    {
      method: 'GET',
      path: '/health',
      description: 'Check server health status',
      responseBody: JSON.stringify({ status: 'ok' }, null, 2),
      curlExample: `curl ${serverUrl}/health`,
    },
    {
      method: 'GET',
      path: '/v1/models',
      description: 'List all loaded models',
      responseBody: JSON.stringify({
        object: 'list',
        data: [
          {
            id: 'gpt-oss-20b',
            object: 'model',
            created: 1234567890,
            owned_by: 'snapllm',
          },
        ],
      }, null, 2),
      curlExample: `curl ${serverUrl}/v1/models`,
    },
    {
      method: 'POST',
      path: '/api/v1/models/load',
      description: 'Load a model into memory',
      requestBody: JSON.stringify({
        model_id: 'my-model',
        file_path: 'D:\\Models\\model.gguf',
        strategy: 'balanced',
      }, null, 2),
      responseBody: JSON.stringify({
        status: 'success',
        message: 'Model loaded: my-model',
        model: 'my-model',
        load_time_ms: 1500,
        cache_only: false,
      }, null, 2),
      curlExample: `curl -X POST ${serverUrl}/api/v1/models/load \\
  -H "Content-Type: application/json" \\
  -d '{
    "model_id": "my-model",
    "file_path": "D:\\\\Models\\\\model.gguf",
    "strategy": "balanced"
  }'`,
    },
    {
      method: 'POST',
      path: '/api/v1/models/switch',
      description: 'Switch active model (<1ms)',
      requestBody: JSON.stringify({
        name: 'my-model',
      }, null, 2),
      responseBody: JSON.stringify({
        status: 'success',
        message: 'Switched to model: my-model',
        model: 'my-model',
        switch_time_ms: 0.42,
      }, null, 2),
      curlExample: `curl -X POST ${serverUrl}/api/v1/models/switch \\
  -H "Content-Type: application/json" \\
  -d '{"name": "my-model"}'`,
    },
    {
      method: 'POST',
      path: '/api/v1/models/unload',
      description: 'Unload a model from memory',
      requestBody: JSON.stringify({
        name: 'my-model',
      }, null, 2),
      responseBody: JSON.stringify({
        status: 'success',
        message: 'Model unloaded: my-model',
        current_model: null,
      }, null, 2),
      curlExample: `curl -X POST ${serverUrl}/api/v1/models/unload \\
  -H "Content-Type: application/json" \\
  -d '{"name": "my-model"}'`,
    },
    {
      method: 'POST',
      path: '/v1/chat/completions',
      description: 'Generate chat completion (OpenAI-compatible)',
      requestBody: JSON.stringify({
        model: 'gpt-oss-20b',
        messages: [
          { role: 'user', content: 'What is the capital of France?' },
        ],
        max_tokens: 512,  // Default: 100 (increase for longer responses)
        stream: false,
      }, null, 2),
      responseBody: JSON.stringify({
        id: 'chatcmpl-123',
        object: 'chat.completion',
        created: 1234567890,
        model: 'gpt-oss-20b',
        choices: [
          {
            index: 0,
            message: {
              role: 'assistant',
              content: 'The capital of France is Paris.',
            },
            finish_reason: 'stop',
          },
        ],
        usage: {
          prompt_tokens: 10,
          completion_tokens: 8,
          total_tokens: 18,
          tokens_per_second: 15.23,
        },
      }, null, 2),
      curlExample: `curl -X POST ${serverUrl}/v1/chat/completions \\
  -H "Content-Type: application/json" \\
  -d '{
    "model": "gpt-oss-20b",
    "messages": [
      {"role": "user", "content": "What is the capital of France?"}
    ],
    "max_tokens": 512,
    "stream": false
  }'`,
    },
    {
      method: 'POST',
      path: '/v1/chat/completions',
      description: 'Streaming chat completion (set stream: true)',
      requestBody: JSON.stringify({
        model: 'gpt-oss-20b',
        messages: [
          { role: 'user', content: 'Tell me a story' },
        ],
        stream: true,
      }, null, 2),
      responseBody: `data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Once"}}]}

data: {"id":"chatcmpl-123","choices":[{"delta":{"content":" upon"}}]}

data: {"id":"chatcmpl-123","choices":[{"delta":{"content":" a"}}]}

data: [DONE]`,
      curlExample: `curl -X POST ${serverUrl}/v1/chat/completions \\
  -H "Content-Type: application/json" \\
  -d '{
    "model": "gpt-oss-20b",
    "messages": [{"role": "user", "content": "Tell me a story"}],
    "stream": true
  }'`,
    },
    {
      method: 'GET',
      path: '/api/v1/models/cache/stats',
      description: 'Get vPID cache statistics',
      responseBody: JSON.stringify({
        status: 'success',
        models: [
          {
            model_id: 'my-model',
            processing_hits: 150,
            processing_misses: 20,
            processing_hit_rate: 0.88,
            memory_usage_mb: 512,
          },
        ],
        total_memory_mb: 512,
      }, null, 2),
      curlExample: `curl ${serverUrl}/api/v1/models/cache/stats`,
    },
    {
      method: 'POST',
      path: '/api/v1/generate',
      description: 'Generate text from a prompt (non-chat)',
      requestBody: JSON.stringify({
        prompt: 'Write a haiku about AI',
        max_tokens: 256,
        temperature: 0.7,
      }, null, 2),
      responseBody: JSON.stringify({
        status: 'success',
        prompt: 'Write a haiku about AI',
        generated_text: 'Silicon dreams flow\nNeural pathways light the dark\nIntelligence blooms',
        model: 'my-model',
        generation_time_s: 0.85,
        tokens_per_second: 45.2,
      }, null, 2),
      curlExample: `curl -X POST ${serverUrl}/api/v1/generate \\
  -H "Content-Type: application/json" \\
  -d '{
    "prompt": "Write a haiku about AI",
    "max_tokens": 256,
    "temperature": 0.7
  }'`,
    },
  ];

  const CopyButton = ({ text, id }: { text: string; id: string }) => (
    <button
      onClick={() => copyToClipboard(text, id)}
      className="p-2 text-gray-500 hover:text-gray-700 hover:bg-gray-100 rounded transition-colors"
      title="Copy to clipboard"
    >
      {copiedId === id ? (
        <CheckCircle className="w-4 h-4 text-green-600" />
      ) : (
        <Copy className="w-4 h-4" />
      )}
    </button>
  );

  return (
    <div className="p-6 max-w-6xl mx-auto">
      {/* Header */}
      <div className="mb-8">
        <div className="flex items-center gap-3 mb-2">
          <Book className="w-8 h-8 text-indigo-600" />
          <h1 className="text-3xl font-bold text-gray-900">API Reference</h1>
        </div>
        <p className="text-gray-600">
          Complete API documentation for SnapLLM multi-model serving platform
        </p>
      </div>

      {/* Server Info */}
      <div className="bg-indigo-50 border border-indigo-200 rounded-lg p-6 mb-8">
        <div className="flex items-center gap-3 mb-4">
          <Server className="w-6 h-6 text-indigo-600" />
          <h2 className="text-xl font-semibold text-gray-900">Server Information</h2>
        </div>
        <div className="space-y-3">
          <div>
            <label className="text-sm font-medium text-gray-700 block mb-1">
              Base URL
            </label>
            <div className="flex items-center gap-2 bg-white border border-gray-300 rounded-lg p-3">
              <code className="flex-1 text-indigo-600 font-mono text-sm">
                {serverUrl}
              </code>
              <CopyButton text={serverUrl} id="base-url" />
            </div>
          </div>
          <div className="text-sm text-gray-600">
            <p className="mb-2">
              <strong>OpenAI Compatibility:</strong> All endpoints under{' '}
              <code className="bg-white px-2 py-1 rounded text-indigo-600">/v1/*</code>{' '}
              follow OpenAI API conventions
            </p>
            <p>
              <strong>Chain-of-Thought:</strong> Reasoning tags like{' '}
              <code className="bg-white px-2 py-1 rounded">&lt;think&gt;</code> and{' '}
              <code className="bg-white px-2 py-1 rounded">&lt;|channel|&gt;</code> are preserved
              in responses for AI systems
            </p>
          </div>
        </div>
      </div>

      {/* Endpoints */}
      <div className="space-y-6">
        {endpoints.map((endpoint, idx) => (
          <div
            key={idx}
            className="bg-white border border-gray-200 rounded-lg overflow-hidden"
          >
            {/* Endpoint Header */}
            <div className="bg-gray-50 border-b border-gray-200 p-4">
              <div className="flex items-center gap-3 mb-2">
                <span
                  className={`px-3 py-1 rounded-md text-xs font-semibold ${
                    endpoint.method === 'GET'
                      ? 'bg-green-100 text-green-800'
                      : 'bg-blue-100 text-blue-800'
                  }`}
                >
                  {endpoint.method}
                </span>
                <code className="text-sm font-mono text-gray-900">
                  {endpoint.path}
                </code>
              </div>
              <p className="text-sm text-gray-600">{endpoint.description}</p>
            </div>

            {/* Endpoint Body */}
            <div className="p-4 space-y-4">
              {/* Request */}
              {endpoint.requestBody && (
                <div>
                  <div className="flex items-center justify-between mb-2">
                    <h4 className="text-sm font-semibold text-gray-700">Request Body</h4>
                    <CopyButton text={endpoint.requestBody} id={`req-${idx}`} />
                  </div>
                  <pre className="bg-gray-900 text-gray-100 p-4 rounded-lg overflow-x-auto text-xs">
                    <code>{endpoint.requestBody}</code>
                  </pre>
                </div>
              )}

              {/* Response */}
              {endpoint.responseBody && (
                <div>
                  <div className="flex items-center justify-between mb-2">
                    <h4 className="text-sm font-semibold text-gray-700">Response</h4>
                    <CopyButton text={endpoint.responseBody} id={`res-${idx}`} />
                  </div>
                  <pre className="bg-gray-900 text-gray-100 p-4 rounded-lg overflow-x-auto text-xs">
                    <code>{endpoint.responseBody}</code>
                  </pre>
                </div>
              )}

              {/* cURL Example */}
              <div>
                <div className="flex items-center justify-between mb-2">
                  <h4 className="text-sm font-semibold text-gray-700">cURL Example</h4>
                  <CopyButton text={endpoint.curlExample} id={`curl-${idx}`} />
                </div>
                <pre className="bg-gray-900 text-gray-100 p-4 rounded-lg overflow-x-auto text-xs">
                  <code>{endpoint.curlExample}</code>
                </pre>
              </div>
            </div>
          </div>
        ))}
      </div>

      {/* Footer Notes */}
      <div className="mt-8 p-6 bg-gray-50 border border-gray-200 rounded-lg">
        <h3 className="text-lg font-semibold text-gray-900 mb-3">Integration Notes</h3>
        <ul className="space-y-2 text-sm text-gray-700">
          <li className="flex items-start gap-2">
            <span className="text-indigo-600 font-bold">•</span>
            <span>
              <strong>OpenAI SDK Compatible:</strong> Use the OpenAI Python/JavaScript SDK by
              setting <code className="bg-white px-2 py-1 rounded">base_url="{serverUrl}"</code>
            </span>
          </li>
          <li className="flex items-start gap-2">
            <span className="text-indigo-600 font-bold">•</span>
            <span>
              <strong>Streaming:</strong> Set <code className="bg-white px-2 py-1 rounded">stream: true</code>{' '}
              for token-by-token responses using Server-Sent Events (SSE)
            </span>
          </li>
          <li className="flex items-start gap-2">
            <span className="text-indigo-600 font-bold">•</span>
            <span>
              <strong>Chain-of-Thought:</strong> Reasoning tags are preserved. Filter display-side using{' '}
              <code className="bg-white px-2 py-1 rounded">filterChainOfThought()</code> utility
            </span>
          </li>
          <li className="flex items-start gap-2">
            <span className="text-indigo-600 font-bold">•</span>
            <span>
              <strong>Caching Strategies:</strong> Choose "minimal", "balanced", "aggressive", or "full"
              based on your memory constraints
            </span>
          </li>
          <li className="flex items-start gap-2">
            <span className="text-indigo-600 font-bold">•</span>
            <span>
              <strong>Max Tokens:</strong> Default is 100 tokens if not specified. For longer responses,
              set <code className="bg-white px-2 py-1 rounded">max_tokens: 512</code> or higher (up to 8192)
            </span>
          </li>
        </ul>
      </div>
    </div>
  );
}
