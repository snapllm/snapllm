# SnapLLM Developer Guide

<p align="center">
  <a href="https://aroora.ai">
    <img src="logo_files/AROORA_315x91.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>

Complete API reference and integration guide for developers.

---

## Table of Contents

- [Overview](#overview)
- [API Compatibility](#api-compatibility)
- [Authentication](#authentication)
- [OpenAI-Compatible API](#openai-compatible-api)
- [Anthropic/Claude Code API](#anthropicclaude-code-api)
- [Model Management API](#model-management-api)
- [Context API (vPID L2)](#context-api-vpid-l2)
- [Vision API (Multimodal)](#vision-api-multimodal)
- [Image Generation API](#image-generation-api)
- [Streaming](#streaming)
- [Error Handling](#error-handling)
- [SDK Integration](#sdk-integration)
- [Best Practices](#best-practices)

---

## Overview

SnapLLM provides a unified HTTP API that is compatible with both **OpenAI** and **Anthropic/Claude** API formats. This allows you to use existing SDKs and tools with SnapLLM as a drop-in replacement.

### Base URL

```
http://localhost:6930
```

### Key Features

| Feature | Description |
|---------|-------------|
| **OpenAI Compatibility** | `/v1/chat/completions` - Works with OpenAI SDKs |
| **Anthropic Compatibility** | `/v1/messages` - Works with Claude Code and Anthropic SDK |
| **<1ms Model Switching** | Switch between loaded models instantly |
| **Tool Calling** | Function/tool calling support for agents |
| **Extended Thinking** | Step-by-step reasoning before response |
| **Streaming** | Server-Sent Events (SSE) for real-time responses |
| **Context Caching** | vPID L2 KV cache persistence for O(1) queries |

---

## API Compatibility

### Supported API Formats

| Format | Endpoint | SDK Support |
|--------|----------|-------------|
| OpenAI | `/v1/chat/completions` | openai-python, openai-node |
| Anthropic | `/v1/messages` | anthropic-python, Claude Code CLI |
| Native | `/api/v1/*` | Direct REST calls |

### Using with Claude Code CLI

```bash
# Set environment variables
export ANTHROPIC_BASE_URL=http://localhost:6930
export ANTHROPIC_AUTH_TOKEN=snapllm

# Run Claude Code with your local model
claude --model your-model-name
```

### Using with OpenAI SDK

```python
from openai import OpenAI

client = OpenAI(
    base_url="http://localhost:6930/v1",
    api_key="snapllm"  # Any value works
)

response = client.chat.completions.create(
    model="your-model-name",
    messages=[{"role": "user", "content": "Hello!"}]
)
```

### Using with Anthropic SDK

```python
import anthropic

client = anthropic.Anthropic(
    base_url="http://localhost:6930",
    api_key="snapllm"  # Any value works
)

response = client.messages.create(
    model="your-model-name",
    max_tokens=1024,
    messages=[{"role": "user", "content": "Hello!"}]
)
```

---

## Authentication

SnapLLM does not require authentication by default. Any value can be used for API keys:

```bash
# All of these work
curl -H "Authorization: Bearer anything"
curl -H "x-api-key: anything"
curl -H "anthropic-api-key: anything"
```

For production deployments, implement authentication at the reverse proxy level (nginx, Cloudflare, etc.).

---

## OpenAI-Compatible API

### Chat Completions

```
POST /v1/chat/completions
```

#### Request

```json
{
  "model": "your-model-name",
  "messages": [
    {"role": "system", "content": "You are a helpful assistant."},
    {"role": "user", "content": "What is machine learning?"}
  ],
  "max_tokens": 512,
  "temperature": 0.7,
  "top_p": 0.9,
  "stream": false,
  "stop": ["\n\n"]
}
```

#### Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `model` | string | Yes | - | Model ID to use |
| `messages` | array | Yes | - | Array of message objects |
| `max_tokens` | integer | No | 256 | Maximum tokens to generate |
| `temperature` | float | No | 0.7 | Sampling temperature (0-2) |
| `top_p` | float | No | 0.9 | Nucleus sampling threshold |
| `stream` | boolean | No | false | Enable streaming response |
| `stop` | array | No | [] | Stop sequences |

#### Response

```json
{
  "id": "chatcmpl-abc123def456",
  "object": "chat.completion",
  "created": 1706123456,
  "model": "your-model-name",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "Machine learning is a subset of artificial intelligence..."
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 25,
    "completion_tokens": 150,
    "total_tokens": 175,
    "tokens_per_second": 65.5
  }
}
```

### List Models

```
GET /v1/models
```

#### Response

```json
{
  "object": "list",
  "data": [
    {
      "id": "llama3",
      "object": "model",
      "created": 1706123456,
      "owned_by": "local"
    },
    {
      "id": "gemma",
      "object": "model",
      "created": 1706123456,
      "owned_by": "local"
    }
  ]
}
```

---

## Anthropic/Claude Code API

SnapLLM fully implements the Anthropic Messages API, enabling use with Claude Code and the Anthropic SDK.

### Messages API

```
POST /v1/messages
```

#### Basic Request

```json
{
  "model": "your-model-name",
  "max_tokens": 1024,
  "messages": [
    {"role": "user", "content": "Explain quantum computing."}
  ]
}
```

#### With System Prompt

```json
{
  "model": "your-model-name",
  "max_tokens": 1024,
  "system": "You are a physics professor specializing in quantum mechanics.",
  "messages": [
    {"role": "user", "content": "Explain quantum computing."}
  ]
}
```

#### Response

```json
{
  "id": "msg_abc123def456",
  "type": "message",
  "role": "assistant",
  "content": [
    {
      "type": "text",
      "text": "Quantum computing harnesses the principles of quantum mechanics..."
    }
  ],
  "model": "your-model-name",
  "stop_reason": "end_turn",
  "usage": {
    "input_tokens": 25,
    "output_tokens": 150
  }
}
```

### Tool Calling (Function Calling)

SnapLLM supports Anthropic-style tool calling for building agentic applications.

#### Request with Tools

```json
{
  "model": "your-model-name",
  "max_tokens": 4096,
  "tools": [
    {
      "name": "get_weather",
      "description": "Get the current weather for a location",
      "input_schema": {
        "type": "object",
        "properties": {
          "location": {
            "type": "string",
            "description": "City name, e.g., 'San Francisco, CA'"
          },
          "unit": {
            "type": "string",
            "enum": ["celsius", "fahrenheit"],
            "description": "Temperature unit"
          }
        },
        "required": ["location"]
      }
    },
    {
      "name": "search_web",
      "description": "Search the web for information",
      "input_schema": {
        "type": "object",
        "properties": {
          "query": {
            "type": "string",
            "description": "Search query"
          }
        },
        "required": ["query"]
      }
    }
  ],
  "messages": [
    {"role": "user", "content": "What's the weather in Tokyo and search for best restaurants there?"}
  ]
}
```

#### Response with Tool Use

```json
{
  "id": "msg_abc123",
  "type": "message",
  "role": "assistant",
  "content": [
    {
      "type": "text",
      "text": "I'll help you with that. Let me check the weather and search for restaurants in Tokyo."
    },
    {
      "type": "tool_use",
      "id": "toolu_weather_001",
      "name": "get_weather",
      "input": {
        "location": "Tokyo, Japan",
        "unit": "celsius"
      }
    },
    {
      "type": "tool_use",
      "id": "toolu_search_001",
      "name": "search_web",
      "input": {
        "query": "best restaurants in Tokyo 2024"
      }
    }
  ],
  "stop_reason": "tool_use",
  "usage": {
    "input_tokens": 150,
    "output_tokens": 85
  }
}
```

#### Sending Tool Results

```json
{
  "model": "your-model-name",
  "max_tokens": 4096,
  "messages": [
    {"role": "user", "content": "What's the weather in Tokyo?"},
    {
      "role": "assistant",
      "content": [
        {"type": "text", "text": "Let me check the weather."},
        {"type": "tool_use", "id": "toolu_001", "name": "get_weather", "input": {"location": "Tokyo"}}
      ]
    },
    {
      "role": "user",
      "content": [
        {
          "type": "tool_result",
          "tool_use_id": "toolu_001",
          "content": "Currently 22°C, partly cloudy with 60% humidity"
        }
      ]
    }
  ]
}
```

### Extended Thinking

Enable step-by-step reasoning before the final response:

#### Request with Thinking

```json
{
  "model": "your-model-name",
  "max_tokens": 8192,
  "thinking": {
    "type": "enabled",
    "budget_tokens": 2048
  },
  "messages": [
    {"role": "user", "content": "Solve this step by step: If a train travels 120 km in 2 hours, what is its average speed?"}
  ]
}
```

#### Response with Thinking

```json
{
  "id": "msg_abc123",
  "type": "message",
  "role": "assistant",
  "content": [
    {
      "type": "thinking",
      "thinking": "Let me solve this step by step:\n\n1. Given information:\n   - Distance = 120 km\n   - Time = 2 hours\n\n2. Formula for average speed:\n   Speed = Distance / Time\n\n3. Calculation:\n   Speed = 120 km / 2 hours\n   Speed = 60 km/h"
    },
    {
      "type": "text",
      "text": "The train's average speed is **60 km/h**.\n\nTo calculate this, I used the formula: Speed = Distance ÷ Time\n\n120 km ÷ 2 hours = 60 km/h"
    }
  ],
  "stop_reason": "end_turn",
  "usage": {
    "input_tokens": 30,
    "output_tokens": 120
  }
}
```

### Streaming (Messages API)

```json
{
  "model": "your-model-name",
  "max_tokens": 1024,
  "stream": true,
  "messages": [
    {"role": "user", "content": "Write a haiku about coding."}
  ]
}
```

#### Streaming Events

```
event: message_start
data: {"type":"message_start","message":{"id":"msg_abc123","type":"message","role":"assistant","content":[],"model":"your-model-name"}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Lines"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" of"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" code"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":25}}

event: message_stop
data: {"type":"message_stop"}
```

---

## Model Management API

### Load Model

```
POST /api/v1/models/load
```

#### Request

```json
{
  "model_id": "llama3",
  "file_path": "D:/Models/llama-3-8b-instruct.Q5_K_M.gguf",
  "model_type": "auto",
  "gpu_layers": -1
}
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `model_id` | string | Yes | - | Unique identifier for the model |
| `file_path` | string | Yes | - | Absolute path to GGUF file |
| `model_type` | string | No | "auto" | "auto", "llm", "vision", "diffusion" |
| `gpu_layers` | integer | No | -1 | Layers on GPU (-1 = all) |
| `mmproj_path` | string | No | - | Vision model projector path |

#### Response

```json
{
  "status": "success",
  "message": "Model loaded: llama3",
  "model": "llama3",
  "model_type": "Text LLM",
  "load_time_ms": 2500.5,
  "active": true
}
```

### Switch Model

```
POST /api/v1/models/switch
```

This is SnapLLM's superpower - switching takes **<1ms**!

#### Request

```json
{
  "model_id": "gemma"
}
```

#### Response

```json
{
  "status": "success",
  "message": "Switched to model: gemma",
  "model": "gemma",
  "switch_time_ms": 0.02
}
```

### List Models (Extended)

```
GET /api/v1/models
```

#### Response

```json
{
  "models": [
    {
      "id": "llama3",
      "name": "Llama 3 8B Instruct",
      "type": "llm",
      "quantization": "Q5_K_M",
      "size_gb": 5.5,
      "active": false,
      "loaded_at": "2024-01-27T10:30:00Z"
    },
    {
      "id": "gemma",
      "name": "Gemma 2 9B",
      "type": "llm",
      "quantization": "Q5_K_M",
      "size_gb": 6.5,
      "active": true,
      "loaded_at": "2024-01-27T10:32:00Z"
    }
  ],
  "active_model": "gemma",
  "total_loaded": 2
}
```

### Unload Model

```
POST /api/v1/models/unload
```

#### Request

```json
{
  "model_id": "llama3"
}
```

#### Response

```json
{
  "status": "success",
  "message": "Model unloaded: llama3"
}
```

### Cache Statistics

```
GET /api/v1/models/cache/stats
```

#### Response

```json
{
  "stats": {
    "total_models_loaded": 3,
    "gpu_memory_used_mb": 4500,
    "cpu_memory_used_mb": 2000,
    "cache_hits": 150,
    "cache_misses": 5,
    "hit_rate": 0.97
  }
}
```

---

## Context API (vPID L2)

The Context API enables O(1) query complexity by pre-computing KV caches.

### Ingest Context

```
POST /api/v1/contexts/ingest
```

#### Request

```json
{
  "content": "Your large document text here...",
  "model_id": "llama3",
  "name": "company-handbook",
  "ttl_seconds": 86400
}
```

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `content` | string | Yes | - | Document text to ingest |
| `model_id` | string | Yes | - | Model to use for KV computation |
| `name` | string | No | auto | Human-readable name |
| `ttl_seconds` | integer | No | 86400 | Time-to-live in seconds |

#### Response

```json
{
  "status": "success",
  "context_id": "ctx_abc123def456",
  "token_count": 5000,
  "storage_size_mb": 125.5,
  "tier": "hot",
  "ingest_time_ms": 1500.5
}
```

### Query Context

```
POST /api/v1/contexts/{context_id}/query
```

#### Request

```json
{
  "query": "What is the vacation policy?",
  "max_tokens": 256
}
```

#### Response

```json
{
  "status": "success",
  "context_id": "ctx_abc123def456",
  "response": "According to the handbook, employees receive 20 days of paid vacation...",
  "cache_hit": true,
  "usage": {
    "context_tokens": 5000,
    "query_tokens": 8,
    "generated_tokens": 45
  },
  "latency_ms": 15.2
}
```

### List Contexts

```
GET /api/v1/contexts
GET /api/v1/contexts?tier=hot
GET /api/v1/contexts?model_id=llama3
```

### Promote/Demote Context

```
POST /api/v1/contexts/{context_id}/promote
POST /api/v1/contexts/{context_id}/demote
```

#### Request

```json
{
  "tier": "hot"  // or "warm" or "cold"
}
```

### Delete Context

```
DELETE /api/v1/contexts/{context_id}
```

### Context Statistics

```
GET /api/v1/contexts/stats
```

#### Response

```json
{
  "stats": {
    "total_contexts": 5,
    "hot_contexts": 2,
    "warm_contexts": 1,
    "cold_contexts": 2,
    "total_memory_mb": 150.5,
    "cache_hits": 100,
    "cache_misses": 5,
    "hit_rate": 0.95,
    "avg_query_latency_ms": 12.3
  }
}
```

---

## Vision API (Multimodal)

### Load Vision Model

Vision models require both a main model and a multimodal projector (mmproj) file.

```
POST /api/v1/models/load
```

```json
{
  "model_id": "gemma-vision",
  "file_path": "D:/Models/gemma-3-4b-it-Q5_K_M.gguf",
  "model_type": "vision",
  "mmproj_path": "D:/Models/mmproj-gemma-3-4b-F16.gguf"
}
```

### Vision Generation

```
POST /api/v1/vision/generate
```

#### Request

```json
{
  "prompt": "Describe this image in detail.",
  "images": ["<base64-encoded-image-data>"],
  "max_tokens": 512
}
```

#### Response

```json
{
  "status": "success",
  "response": "The image shows a beautiful sunset over a mountain range...",
  "model": "gemma-vision",
  "generation_time_s": 2.5,
  "tokens_per_second": 57.5
}
```

### Supported Vision Models

| Model | Status | Notes |
|-------|--------|-------|
| Gemma 3 4B/27B | ✅ Supported | Requires mmproj file |
| Qwen2.5-Omni | ✅ Supported | Requires mmproj file |
| LLaVA | ✅ Supported | Requires mmproj file |
| Janus Pro | ❌ Not supported | Projector type not in llama.cpp |

---

## Image Generation API

### Generate Image

```
POST /api/v1/diffusion/generate
```

#### Request

```json
{
  "prompt": "A futuristic city at sunset, cyberpunk style, highly detailed",
  "negative_prompt": "blurry, low quality, distorted",
  "width": 512,
  "height": 512,
  "steps": 20,
  "cfg_scale": 7.0,
  "seed": -1
}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `prompt` | string | - | Image description |
| `negative_prompt` | string | "" | What to avoid |
| `width` | integer | 512 | Image width |
| `height` | integer | 512 | Image height |
| `steps` | integer | 20 | Sampling steps |
| `cfg_scale` | float | 7.0 | Classifier-free guidance |
| `seed` | integer | -1 | Random seed (-1 = random) |

#### Response

```json
{
  "status": "success",
  "image": "<base64-encoded-image>",
  "seed": 42,
  "generation_time_s": 8.5
}
```

---

## Streaming

### OpenAI Format Streaming

```bash
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "llama3",
    "messages": [{"role": "user", "content": "Count to 5"}],
    "stream": true
  }'
```

Response (Server-Sent Events):
```
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"llama3","choices":[{"index":0,"delta":{"role":"assistant"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"llama3","choices":[{"index":0,"delta":{"content":"1"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"llama3","choices":[{"index":0,"delta":{"content":", "},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","created":1234567890,"model":"llama3","choices":[{"index":0,"delta":{"content":"2"},"finish_reason":null}]}

data: [DONE]
```

### Anthropic Format Streaming

See the [Anthropic Streaming section](#streaming-messages-api) above.

---

## Error Handling

### Error Response Format

```json
{
  "error": {
    "type": "invalid_request_error",
    "message": "Model not found: unknown-model",
    "code": "model_not_found"
  }
}
```

### Common Error Codes

| Code | HTTP Status | Description |
|------|-------------|-------------|
| `model_not_found` | 404 | Requested model is not loaded |
| `invalid_request` | 400 | Malformed request body |
| `context_not_found` | 404 | Context ID doesn't exist |
| `file_not_found` | 400 | Model file path doesn't exist |
| `out_of_memory` | 500 | Insufficient GPU/CPU memory |
| `generation_failed` | 500 | Error during text generation |

---

## SDK Integration

### Python (OpenAI SDK)

```python
from openai import OpenAI

client = OpenAI(
    base_url="http://localhost:6930/v1",
    api_key="snapllm"
)

# Non-streaming
response = client.chat.completions.create(
    model="llama3",
    messages=[
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": "Hello!"}
    ],
    max_tokens=256
)
print(response.choices[0].message.content)

# Streaming
stream = client.chat.completions.create(
    model="llama3",
    messages=[{"role": "user", "content": "Count to 10"}],
    stream=True
)
for chunk in stream:
    if chunk.choices[0].delta.content:
        print(chunk.choices[0].delta.content, end="", flush=True)
```

### Python (Anthropic SDK)

```python
import anthropic

client = anthropic.Anthropic(
    base_url="http://localhost:6930",
    api_key="snapllm"
)

# Non-streaming
response = client.messages.create(
    model="llama3",
    max_tokens=1024,
    messages=[{"role": "user", "content": "Hello!"}]
)
print(response.content[0].text)

# Streaming
with client.messages.stream(
    model="llama3",
    max_tokens=1024,
    messages=[{"role": "user", "content": "Count to 10"}]
) as stream:
    for text in stream.text_stream:
        print(text, end="", flush=True)

# Tool calling
response = client.messages.create(
    model="llama3",
    max_tokens=1024,
    tools=[{
        "name": "get_weather",
        "description": "Get weather for a location",
        "input_schema": {
            "type": "object",
            "properties": {
                "location": {"type": "string"}
            },
            "required": ["location"]
        }
    }],
    messages=[{"role": "user", "content": "What's the weather in Paris?"}]
)
```

### JavaScript/TypeScript (OpenAI SDK)

```typescript
import OpenAI from 'openai';

const client = new OpenAI({
  baseURL: 'http://localhost:6930/v1',
  apiKey: 'snapllm'
});

// Non-streaming
const response = await client.chat.completions.create({
  model: 'llama3',
  messages: [{ role: 'user', content: 'Hello!' }]
});
console.log(response.choices[0].message.content);

// Streaming
const stream = await client.chat.completions.create({
  model: 'llama3',
  messages: [{ role: 'user', content: 'Count to 10' }],
  stream: true
});
for await (const chunk of stream) {
  process.stdout.write(chunk.choices[0]?.delta?.content || '');
}
```

### cURL

```bash
# Chat completion
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "llama3",
    "messages": [{"role": "user", "content": "Hello!"}]
  }'

# Anthropic format
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "llama3",
    "max_tokens": 1024,
    "messages": [{"role": "user", "content": "Hello!"}]
  }'
```

---

## Best Practices

### 1. Model Management

```python
# Load models at startup, switch during runtime
# DON'T: Load/unload models for each request
# DO: Load once, switch in <1ms

# Good pattern
load_model("general", "path/to/general.gguf")
load_model("coding", "path/to/coding.gguf")

# During runtime
switch_model("coding")  # <1ms
response = chat("coding", ...)
switch_model("general")  # <1ms
response = chat("general", ...)
```

### 2. Context Caching

```python
# For RAG/document Q&A, ingest once, query many times
# DON'T: Send full document with each query
# DO: Use context API for O(1) queries

# Good pattern
context_id = ingest_context(large_document, "llama3")

# Fast queries (O(1) context lookup)
answer1 = query_context(context_id, "Question 1")
answer2 = query_context(context_id, "Question 2")
answer3 = query_context(context_id, "Question 3")
```

### 3. Streaming

```python
# For user-facing applications, always use streaming
# This provides better perceived latency

response = client.chat.completions.create(
    model="llama3",
    messages=[...],
    stream=True  # Always for user-facing
)
```

### 4. Error Handling

```python
import requests

def safe_chat(model, messages):
    try:
        response = requests.post(
            "http://localhost:6930/v1/chat/completions",
            json={"model": model, "messages": messages},
            timeout=60
        )
        response.raise_for_status()
        return response.json()
    except requests.exceptions.Timeout:
        return {"error": "Request timed out"}
    except requests.exceptions.HTTPError as e:
        return {"error": str(e)}
```

### 5. Memory Management

```python
# Monitor memory usage
stats = requests.get("http://localhost:6930/api/v1/models/cache/stats").json()

if stats["gpu_memory_used_mb"] > 5000:
    # Unload least-used models
    unload_model("rarely-used-model")

# Demote cold contexts to free memory
demote_context(context_id, "cold")
```

---

## API Endpoints Summary

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/v1/models` | GET | List models (OpenAI format) |
| `/v1/chat/completions` | POST | Chat completion (OpenAI) |
| `/v1/messages` | POST | Messages API (Anthropic) |
| `/api/v1/models` | GET | List models (extended) |
| `/api/v1/models/load` | POST | Load a model |
| `/api/v1/models/switch` | POST | Switch active model |
| `/api/v1/models/unload` | POST | Unload a model |
| `/api/v1/models/cache/stats` | GET | Cache statistics |
| `/api/v1/generate` | POST | Text generation |
| `/api/v1/vision/generate` | POST | Vision/multimodal |
| `/api/v1/diffusion/generate` | POST | Image generation |
| `/api/v1/contexts/ingest` | POST | Ingest context |
| `/api/v1/contexts` | GET | List contexts |
| `/api/v1/contexts/{id}/query` | POST | Query context |
| `/api/v1/contexts/{id}` | DELETE | Delete context |
| `/api/v1/contexts/{id}/promote` | POST | Promote tier |
| `/api/v1/contexts/{id}/demote` | POST | Demote tier |
| `/api/v1/contexts/stats` | GET | Context statistics |

---

<p align="center">
  <strong>Happy building!</strong>
</p>
