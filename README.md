<p align="center">
  <img src="logo_files/FULL_TRIMMED_transparent.png" alt="SnapLLM Logo" width="400"/>
</p>

<h1 align="center">High-Performance Multi-Model LLM Inference Engine with Sub-Millisecond Model Switching, </br> Switch models in a snap! with Desktop UI, CLI & API</h1>

<p align="center">
  <strong>Arxiv Paper Link to be added</strong></br>
  <strong>🤩 Star this repository - It helps others discover SnapLLM 🤩</strong>
</p>


<video src="https://github.com/user-attachments/assets/4c06bcb8-cccd-478b-abbc-aa2d4c07db86" autoplay loop muted playsinline width="800"></video>


<p align="center">
  <a href="#features">Features</a> |
  <a href="#installation">Installation</a> |
  <a href="#quick-start">Quick Start</a> |
  <a href="#api-reference">API Reference</a> |
  <a href="#architecture">Architecture</a> |
  <a href="#demo-videos">Demo Videos</a> |
  <a href="#contributing">Contributing</a> |
  <a href="#sponsors">Sponsors</a>
</p>

<p align="center">
  <a href="https://github.com/snapllm/snapllm/releases"><img src="https://img.shields.io/github/v/tag/snapllm/snapllm?label=version&style=flat-square" alt="Version"/></a>
  <a href="https://github.com/snapllm/snapllm/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/snapllm/snapllm/ci.yml?branch=main&style=flat-square" alt="CI"/></a>
  <a href="https://github.com/snapllm/snapllm/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=flat-square" alt="License: MIT"/></a>
  <a href="https://github.com/snapllm/snapllm/stargazers"><img src="https://img.shields.io/github/stars/snapllm/snapllm?style=flat-square" alt="Stars"/></a>
  <a href="https://github.com/snapllm/snapllm/issues"><img src="https://img.shields.io/github/issues/snapllm/snapllm?style=flat-square" alt="Issues"/></a>
  <img src="https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square" alt="PRs welcome"/>
  <img src="https://img.shields.io/badge/C++-17-blue.svg?style=flat-square" alt="C++17"/>
  <img src="https://img.shields.io/badge/CUDA-12.x-green.svg?style=flat-square" alt="CUDA 12.x"/>
  <img src="https://img.shields.io/badge/llama.cpp-latest-orange.svg?style=flat-square" alt="llama.cpp"/>
</p>

<p align="center">
  <em>Developed by Mahesh Vaikri</em><br/>
  <a href="https://www.linkedin.com/company/aroora-ai-labs">
    <img src="logo_files/AROORA_315x88.png" alt="AroorA AI Lab" width="200"/>
</p>

---

## What is SnapLLM?

SnapLLM is a LLM inference engine built on top of llama.cpp, stable-diffusion.cpp that enables **sub-millisecond model switching** between multiple loaded models. Unlike traditional approaches where switching models requires unloading and reloading (taking seconds to minutes), SnapLLM keeps multiple models hot in memory and switches between them in **<1ms**.

### The Problem

Traditional LLM inference:
```
Load Model A: 30-60 seconds
Use Model A
Unload Model A: 5-10 seconds
Load Model B: 30-60 seconds  <- Painful wait!
Use Model B
```

### The SnapLLM Solution

```
Load Model A: 30 seconds (one-time)
Load Model B: 30 seconds (one-time)
Switch A -> B: 0.02ms  <- Instant!
Switch B -> A: 0.02ms  <- Instant!
```

---

## Features

### Core Capabilities

| Feature | Description |
|---------|-------------|
| **Fast <1ms Model Switching** | Switch between loaded models instantly using vPID architecture |
| **Multi-Model Management** | Load and manage multiple models simultaneously |
| **GPU/CPU Hybrid** | Automatic layer distribution based on available VRAM |
| **OpenAI-Compatible API** | Drop-in replacement for OpenAI API |
| **Multi-Modal Support** | LLM, Vision (VLM), and Stable Diffusion models |
| **KV Cache Persistence** | vPID L2 context caching for O(1) query complexity |
| **Desktop Application** | Beautiful React-based UI for model management |

### Supported Model Types

- **Text LLMs**: Llama 1/2/3, Mistral, Mixtral, Qwen, Gemma, Phi, DeepSeek, and all GGUF models
- **Vision Models**: Gemma 3 Vision, Qwen-VL, LLaVA (with mmproj files)
- **Diffusion Models**: Stable Diffusion 1.5, SDXL, SD3, FLUX (via stable-diffusion.cpp)

### Performance Benchmarks

| Metric | Value |
|--------|-------|
| Model Switch Time | **0.02ms** |
| First Token Latency | ~50ms |
| Token Generation | 30-100+ tok/s (GPU dependent) |
| Memory Efficiency | Shared KV cache across contexts |

#### Text LLM Performance (RTX 4060 Laptop GPU)

| Model | Size | Quantization | Speed |
|-------|------|--------------|-------|
| Medicine-LLM | 8B | Q8_0 | 64 tok/s |
| Gemma 3 | 4B | Q5_K_M | 65 tok/s |
| Qwen 3 | 8B | Q8_0 | 58 tok/s |
| Llama 3 | 8B | Q4_K_M | 75 tok/s |

#### Multi-Model Switching Performance

| Operation | Time |
|-----------|------|
| First model load | 2-5s (includes dequantization) |
| Subsequent loads | <100ms (cached) |
| Model switch | **<1ms** |
| Rapid switching (3 models) | <3ms total |

---

## Installation

### Prerequisites

- **Windows**: Visual Studio 2022 with C++ workload
- **Linux**: GCC 11+ or Clang 14+
- **CUDA**: 12.x (for GPU acceleration)
- **CMake**: 3.18+
- **Node.js**: 18+ (for desktop app)

### Build from Source

#### Windows (GPU)

```bash
# Clone the repository
git clone https://github.com/snapllm/snapllm.git
cd snapllm

# Build with CUDA support
build_gpu.bat
```

#### Windows (CPU only)

```bash
build_cpu.bat
```

#### Linux (GPU)

```bash
chmod +x build.sh
./build.sh --cuda
```

### Verify Installation

```bash
# Check the build
build_gpu/bin/snapllm --help

# Start the server
build_gpu/bin/snapllm --server --port 6930
```

---

## How to Start the Server

### Windows (recommended)

```bat
Start_Server.bat
```

This launcher auto-detects the newest build and starts the server on `127.0.0.1:6930`.
If the desktop app is built, it will launch the UI; otherwise it opens the browser.

### Windows (manual)

```bat
build_gpu\bin\snapllm.exe --server --host 127.0.0.1 --port 6930
```

### Start with a model preloaded

```bat
build_gpu\bin\snapllm.exe --server --host 127.0.0.1 --port 6930 --load-model mymodel D:\Models\mymodel.gguf
```

### Linux/macOS

```bash
chmod +x start_server.sh
./start_server.sh
```

### Optional launch overrides (scripts)

- `SNAPLLM_SERVER_EXE`: Full path to the SnapLLM binary
- `SNAPLLM_HOST`: Host bind address (default `127.0.0.1`)
- `SNAPLLM_PORT`: Server port (default `6930`)
- `SNAPLLM_WORKSPACE_ROOT`: Workspace root directory

---

## Quick Start

### 1. Start the Server

```bash
# Start with a model pre-loaded
./snapllm --server --port 6930 --load-model mymodel /path/to/model.gguf

# Or start empty and load models via API
./snapllm --server --port 6930
```

### 2. Load Models via API

```bash
# Load first model
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama3",
    "file_path": "/models/llama-3-8b-instruct.Q5_K_M.gguf"
  }'

# Load second model
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "gemma",
    "file_path": "/models/gemma-2-9b-it.Q5_K_M.gguf"
  }'
```

### 3. Switch Between Models Instantly

```bash
# Switch to gemma (takes ~0.02ms!)
curl -X POST http://localhost:6930/api/v1/models/switch \
  -H "Content-Type: application/json" \
  -d '{"model_id": "gemma"}'
```

### 4. Chat with the Active Model

```bash
# OpenAI-compatible chat endpoint
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "gemma",
    "messages": [
      {"role": "user", "content": "Explain quantum computing in simple terms"}
    ],
    "max_tokens": 256
  }'
```

### 5. Use the Desktop App (UI still in Beta)

```bash
# Start the frontend (requires Node.js)
cd desktop-app
npm install
npm run dev
# Open http://localhost:9780
```

---

## API Reference

### Base URL

```
http://localhost:6930
```

### Endpoints Overview

#### Health & Status

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | API information |
| GET | `/health` | Server health check |
| GET | `/v1/models` | List models (OpenAI format) |
| GET | `/api/v1/models` | List models (extended info) |

#### Model Management

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/models/load` | Load a model |
| POST | `/api/v1/models/switch` | Switch active model (<1ms) |
| POST | `/api/v1/models/unload` | Unload a model |
| DELETE | `/api/v1/models/{id}` | Delete a model |
| POST | `/api/v1/models/scan` | Scan folder for models |

#### Text Generation

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/chat/completions` | Chat completion (OpenAI) |
| POST | `/api/v1/generate` | Text generation |
| POST | `/api/v1/generate/batch` | Batch generation |

#### Vision (Multimodal)

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/vision/generate` | Analyze images |

#### Image Generation

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/diffusion/generate` | Generate images |

#### Context API (vPID L2)

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/contexts/ingest` | Pre-compute KV cache |
| GET | `/api/v1/contexts` | List contexts |
| POST | `/api/v1/contexts/{id}/query` | Query with O(1) lookup |
| DELETE | `/api/v1/contexts/{id}` | Delete context |

### Detailed API Examples

#### Load Model

```bash
POST /api/v1/models/load
```

Request:
```json
{
  "model_id": "medical-llm",
  "file_path": "D:/Models/medicine-llm.Q8_0.gguf",
  "model_type": "auto"
}
```

Response:
```json
{
  "status": "success",
  "message": "Model loaded: medical-llm",
  "model": "medical-llm",
  "model_type": "Text LLM",
  "load_time_ms": 2500.5,
  "active": true
}
```

#### Chat Completion

```bash
POST /v1/chat/completions
```

Request:
```json
{
  "model": "medical-llm",
  "messages": [
    {"role": "system", "content": "You are a medical assistant."},
    {"role": "user", "content": "What are the symptoms of diabetes?"}
  ],
  "max_tokens": 512,
  "temperature": 0.7,
  "stream": false
}
```

Response:
```json
{
  "id": "chatcmpl-abc123",
  "object": "chat.completion",
  "created": 1234567890,
  "model": "medical-llm",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "Common symptoms of diabetes include..."
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 25,
    "completion_tokens": 150,
    "total_tokens": 175,
    "tokens_per_second": 64.5
  }
}
```

#### Load Vision Model

```bash
POST /api/v1/models/load
```

Request:
```json
{
  "model_id": "gemma-vision",
  "file_path": "/models/gemma-3-4b-it.gguf",
  "model_type": "vision",
  "mmproj_path": "/models/mmproj-gemma-3-4b.gguf"
}
```

#### Vision Analysis

```bash
POST /api/v1/vision/generate
```

Request:
```json
{
  "prompt": "Describe this image in detail",
  "images": ["<base64-encoded-image>"],
  "max_tokens": 256
}
```

Response:
```json
{
  "status": "success",
  "response": "The image shows a beautiful sunset over mountains...",
  "model": "gemma-vision",
  "generation_time_s": 2.5,
  "tokens_per_second": 57.5
}
```

#### Context Ingestion (vPID L2)

```bash
POST /api/v1/contexts/ingest
```

Request:
```json
{
  "content": "Your large document text here...",
  "model_id": "llama3",
  "name": "company-handbook",
  "ttl_seconds": 86400
}
```

Response:
```json
{
  "status": "success",
  "context_id": "ctx_abc123",
  "token_count": 5000,
  "storage_size_mb": 125.5,
  "tier": "hot",
  "ingest_time_ms": 1500.5
}
```

#### Query with Cached Context

```bash
POST /api/v1/contexts/ctx_abc123/query
```

Request:
```json
{
  "query": "What is the vacation policy?",
  "max_tokens": 100
}
```

Response:
```json
{
  "status": "success",
  "context_id": "ctx_abc123",
  "response": "According to the handbook, employees receive...",
  "cache_hit": true,
  "latency_ms": 15.2
}
```

---

## Architecture

### vPID (Virtual Processing-In-Disk) System

SnapLLM uses a vPID architecture that enables instant model switching:

```
+--------------------------------------------------------------------+
|                          SnapLLM Server                             |
+--------------------------------------------------------------------+
|  Model A (vPID 1)  |  Model B (vPID 2)  |  Model C (vPID 3)  | ... |
+---------+----------+---------+----------+---------+----------+-----+
          |                    |                    |
          +--------------------+--------------------+
                               |
                               v
+--------------------------------------------------------------------+
|                    Unified Memory Manager                           |
|   HOT (GPU VRAM)    |   WARM (CPU RAM)    |    COLD (SSD)           |
+--------------------------------------------------------------------+
|               HTTP API Server (OpenAI-compatible)                   |
+--------------------------------------------------------------------+
```

Request flow (simplified):

```
Client -> HTTP API -> Router -> Model Manager -> Active Model -> Response
                           |                      ^
                           v                      |
                             vPID Cache (L1/L2) 
```

### vPID Levels

| Level | Name | Purpose |
|-------|------|---------|
| **L1** | Model Cache | Pre-dequantized tensors on disk, fast reload |
| **L2** | Context Cache | KV cache persistence, O(1) query complexity |

### Memory Tiers

| Tier | Storage | Latency | Capacity |
|------|---------|---------|----------|
| **HOT** | GPU VRAM | ~1ms | 6-48GB |
| **WARM** | CPU RAM | ~10ms | 16-128GB |
| **COLD** | SSD | ~100ms | 1TB+ |

### Per-Model Workspace Structure

```
SnapLLM_Workspace/
|-- index.json                          # Model registry
|-- medicine-llm/
|   `-- Q8_0/
|       `-- workspace.bin               # Pre-dequantized tensors
|-- legal-llama/
|   `-- Q4_K_M/
|       `-- workspace.bin
|-- gemma-3-4b/
|   `-- Q5_K_M/
|       `-- workspace.bin
|-- diffusion/
|   `-- sd15/
|       `-- workspace.bin
`-- contexts/                           # vPID L2 KV caches
    |-- hot/
    |-- warm/
    `-- cold/
```


---

## Desktop Application

SnapLLM includes a beautiful, modern desktop application built with React and Vite.

### Features

- **Dashboard**: System overview and health metrics
- **Chat**: Interactive chat with loaded models
- **Images**: Stable Diffusion image generation
- **Vision**: Multimodal image analysis
- **Models**: Load, manage, and switch models
- **A/B Compare**: Side-by-side model comparison
- **Quick Switch**: <1ms model switching interface
- **Contexts**: vPID L2 KV cache management
- **Playground**: API testing interface
- **Metrics**: Performance analytics

### Running the Desktop App

```bash
cd desktop-app
npm install
npm run dev
# Open http://localhost:9780
```

---

## Demo Videos



- [SnapLLM Desktop App Demo (Vimeo)](https://vimeo.com/1157629276?fl=ip&fe=ec)
- [SnapLLM Server and API Demo (Vimeo)](https://vimeo.com/1157624031?fl=ip&fe=ec)

---

## Use Cases

### 1. Multi-Domain Assistant

Load specialized models for different domains and switch instantly:

```python
import requests

BASE_URL = "http://localhost:6930"

# Load domain-specific models
for model in [("medical", "medical.gguf"), ("legal", "legal.gguf"), ("coding", "coding.gguf")]:
    requests.post(f"{BASE_URL}/api/v1/models/load", json={
        "model_id": model[0],
        "file_path": f"/models/{model[1]}"
    })

def ask(domain, question):
    requests.post(f"{BASE_URL}/api/v1/models/switch", json={"model_id": domain})
    response = requests.post(f"{BASE_URL}/v1/chat/completions", json={
        "model": domain,
        "messages": [{"role": "user", "content": question}]
    })
    return response.json()["choices"][0]["message"]["content"]

# Switch domains instantly (<1ms)
print(ask("medical", "What are symptoms of flu?"))
print(ask("legal", "What is intellectual property?"))
print(ask("coding", "Write a Python fibonacci function"))
```

### 2. A/B Model Comparison

Compare responses from different models side-by-side:

```python
models = ["llama3", "gemma2", "mistral"]

def compare(question):
    results = {}
    for model in models:
        requests.post(f"{BASE_URL}/api/v1/models/switch", json={"model_id": model})
        response = requests.post(f"{BASE_URL}/v1/chat/completions", json={
            "model": model,
            "messages": [{"role": "user", "content": question}]
        })
        results[model] = response.json()["choices"][0]["message"]["content"]
    return results

comparison = compare("Explain machine learning in one paragraph")
for model, answer in comparison.items():
    print(f"--- {model} ---\n{answer}\n")
```

### 3. RAG with Context Caching (vPID L2)

Pre-compute KV cache for large documents:

```python
# Ingest a large document (one-time O(n^2) operation)
response = requests.post(f"{BASE_URL}/api/v1/contexts/ingest", json={
    "content": open("large_document.txt").read(),
    "model_id": "llama3",
    "name": "company-handbook"
})
context_id = response.json()["context_id"]

# Query with O(1) context lookup (instant!)
def query_handbook(question):
    response = requests.post(f"{BASE_URL}/api/v1/contexts/{context_id}/query", json={
        "query": question,
        "max_tokens": 256
    })
    return response.json()["response"]

# Queries are now instant because context is pre-computed
print(query_handbook("What is the vacation policy?"))
print(query_handbook("How do I submit expenses?"))
```

---

## CLI Reference

### Server Options

```bash
./snapllm --server [OPTIONS]

Options:
  --port PORT              Server port (default: 6930)
  --host HOST              Bind address (default: 0.0.0.0)
  --cors                   Enable CORS
  --workspace PATH         Workspace directory
  --load-model NAME PATH   Pre-load a model
```

### Text LLM Options

```bash
./snapllm --load-model NAME PATH [OPTIONS]

Options:
  --prompt TEXT            Generate text from prompt
  --max-tokens N           Maximum tokens to generate
  --temperature N          Sampling temperature
  --multi-model-test       Run multi-model switching benchmark
  --list-models            List all loaded models
  --stats                  Show cache statistics
```

### Image Generation Options

```bash
./snapllm --load-diffusion NAME PATH [OPTIONS]

Options:
  --generate-image PROMPT  Generate image from text
  --output PATH            Output image path
  --width N                Image width (default: 512)
  --height N               Image height (default: 512)
  --steps N                Sampling steps (default: 20)
  --cfg-scale N            CFG scale (default: 7.0)
  --seed N                 Random seed (-1 for random)
  --negative PROMPT        Negative prompt
```

### Vision Options

```bash
./snapllm --multimodal MODEL MMPROJ [OPTIONS]

Options:
  --image PATH             Input image file
  --vision-prompt TEXT     Prompt with <__media__> marker
  --max-tokens N           Maximum tokens to generate
```

---

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `SNAPLLM_HOME` | Workspace root directory | Platform default |
| `SNAPLLM_MODELS_PATH` | Default models directory | Platform default |
| `SNAPLLM_CONFIG_PATH` | Server config file path | Platform default |

---

## Contributing

We welcome contributions! Here's how to get started:

### Development Setup

```bash
# Clone the repository
git clone https://github.com/snapllm/snapllm.git
cd snapllm

# Build debug version
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .

# Run tests
ctest --output-on-failure
```

### Code Style

- **C++**: snake_case functions, PascalCase classes, `I*` prefix for interfaces
- **TypeScript**: ESLint + Prettier
- **Commits**: Conventional commits format

### Submitting Changes

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## Sponsors

<p align="center">
  <strong>Support SnapLLM Development</strong>
</p>

SnapLLM is an open-source project maintained by passionate developers. Your sponsorship helps us:

- Develop new features faster
- Fix bugs and improve stability
- Create better documentation
- Maintain infrastructure and CI/CD
- Support the open-source community

### Become a Sponsor

<p align="center">
research@aroora.ai
</p>

### Sponsor Tiers

| Tier | Monthly | Benefits |
|------|---------|----------|
| **Coffee** | $5 | Name in README |
| **Supporter** | $25 | Priority issue response, early access |
| **Pro** | $100 | Roadmap input, feature voting |
| **Enterprise** | $500 | Custom feature requests, onboarding help |
| **Corporate** | $2000+ | Logo placement, consulting hours |

### Corporate Sponsors

<p align="center">
  <!-- Add corporate sponsor logos here -->
  <em>Your company logo could be here!</em>
</p>

### Individual Sponsors

Thank you to our amazing individual sponsors:

<!-- sponsors -->
<!-- Add sponsor names here -->
<!-- /sponsors -->

### Other Ways to Support

- **Star this repository** - It helps others discover SnapLLM
- **Report bugs** - Help us improve quality
- **Improve documentation** - Clear docs help everyone
- **Join discussions** - Share ideas and feedback
- **Spread the word** - Share SnapLLM with others

---

## License

SnapLLM is released under the [MIT License](LICENSE).

```
MIT License

Copyright (c) 2024-2026 SnapLLM Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
```

---

## Acknowledgments

SnapLLM is built on the shoulders of giants:

- [llama.cpp](https://github.com/ggerganov/llama.cpp) - The foundation for LLM inference
- [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) - Diffusion model support
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) - HTTP server library
- NVIDIA CUDA - GPU acceleration

---

<p align="center">
  <strong>Switch models in a snap!</strong>
</p>

<p align="center">
  <a href="https://www.linkedin.com/company/aroora-ai-labs">
    <img src="logo_files/AROORA_315x88.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>
<p align="center">
  </a>
  Follow SnapLLM in LinkedIn <br/>
    <a href="https://www.linkedin.com/company/snapllm">
    <img src="logo_files/SnapLLM_Small_transparent_320x103.png" alt="SnapLLM LinkedIn" width="200"/>
  </a>
</p>
<p align="center">
  Creator: Mahesh Vaikri <br/>
  Developed by <strong>AroorA AI Labs</strong><br/>
  Made with care for the AI community
</p>

<p align="center">
  <a href="https://github.com/snapllm/snapllm/issues">Report Bug</a> |
  <a href="https://github.com/snapllm/snapllm/issues">Request Feature</a> |
</p>


