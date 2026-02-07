# SnapLLM Quick Start Guide
<p align="center">
  <img src="logo_files/FULL_TRIMMED_transparent.png" alt="SnapLLM Logo" width="400"/>
</p>

<h1 align="center">High-Performance Multi-Model LLM Inference Engine with Sub-Millisecond Model Switching, </br> Switch models in a snap! with Desktop UI, CLI & API</h1>

<p align="center">
  <strong>Arxiv Paper Link to be added</strong>
</p>
<p align="center">
  <a href="https://www.linkedin.com/company/aroora-ai-labs">
    <img src="logo_files/AROORA_315x88.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>

Get up and running with SnapLLM in under 5 minutes.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Option 1: Download Pre-built Release](#option-1-download-pre-built-release)
- [Option 2: Build from Source](#option-2-build-from-source)
- [Running the Server](#running-the-server)
- [Loading Your First Model](#loading-your-first-model)
- [Making Your First Request](#making-your-first-request)
- [Multi-Model Switching](#multi-model-switching)
- [Using the Desktop App](#using-the-desktop-app)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Hardware Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| GPU | NVIDIA GTX 1060 6GB | RTX 3060 12GB+ |
| RAM | 16 GB | 32 GB |
| Storage | 10 GB free | 50 GB+ (for models) |

### Software Requirements

- **Windows 10/11** or **Linux** (Ubuntu 20.04+)
- **NVIDIA GPU Driver** 525+ (for CUDA support)
- **CUDA Toolkit 12.x** (included in release package for Windows)

### Getting Models

Download GGUF models from:
- [Hugging Face](https://huggingface.co/models?sort=trending&search=gguf)
- [TheBloke's Models](https://huggingface.co/TheBloke)

Recommended starter models:
- `Llama-3-8B-Instruct-Q5_K_M.gguf` (~5.5 GB)
- `Mistral-7B-Instruct-v0.2-Q5_K_M.gguf` (~5 GB)
- `Gemma-2-9B-it-Q5_K_M.gguf` (~6.5 GB)

---

## Option 1: Download Pre-built Release

### Step 1: Download

Download the latest release from the [Releases](https://github.com/snapllm/snapllm/releases) page.

Choose:
- `snapllm-X.X.X-windows-x64-cuda.zip` for Windows with GPU
- `snapllm-X.X.X-linux-x64-cuda.tar.gz` for Linux with GPU

### Step 2: Extract

```bash
# Windows (PowerShell)
Expand-Archive snapllm-X.X.X-windows-x64-cuda.zip -DestinationPath snapllm

# Linux
tar -xzf snapllm-X.X.X-linux-x64-cuda.tar.gz
cd snapllm
```

### Step 3: Run

```bash
# Windows
run_server.bat

# Linux
./run_server.sh
```

---

## Option 2: Build from Source

### Step 1: Clone Repository

```bash
git clone --recursive https://github.com/snapllm/snapllm.git
cd snapllm
```

### Step 2: Build

**Windows (GPU):**
```cmd
build_gpu.bat
```

**Windows (CPU only):**
```cmd
build_cpu.bat
```

**Linux (GPU):**
```bash
chmod +x build.sh
./build.sh --cuda
```

**Linux (CPU only):**
```bash
./build.sh
```

### Step 3: Verify Build

```bash
# Windows
build_gpu\bin\snapllm.exe --help

# Linux
./build_gpu/bin/snapllm --help
```

---

## Running the Server

### Basic Server Start

```bash
# Windows
build_gpu\bin\snapllm.exe --server --port 6930

# Linux
./build_gpu/bin/snapllm --server --port 6930
```

You should see:
```
================================================================
  SnapLLM HTTP Server v1.0.0
================================================================
  Listening on: http://0.0.0.0:6930
  Workspace:    C:\Users\YourName\SnapLLM_Workspace  (Windows)
                /home/yourname/SnapLLM_Workspace     (Linux)
  CORS:         enabled
================================================================
```

> **Note:** The workspace directory is automatically created in your home folder.
> - **Windows**: `%USERPROFILE%\SnapLLM_Workspace` (e.g., `C:\Users\YourName\SnapLLM_Workspace`)
> - **Linux/macOS**: `~/SnapLLM_Workspace` (e.g., `/home/yourname/SnapLLM_Workspace`)
>
> To use a custom location: `snapllm --server --workspace-root "D:\MyWorkspace"`

### Verify Server is Running

```bash
curl http://localhost:6930/health
```

Response:
```json
{"status": "healthy", "version": "1.0.0"}
```

---

## Loading Your First Model

### Via Command Line (at startup)

```bash
# Windows
build_gpu\bin\snapllm.exe --server --port 6930 --load-model mymodel "D:\Models\llama-3-8b.gguf"

# Linux
./build_gpu/bin/snapllm --server --port 6930 --load-model mymodel "/home/user/models/llama-3-8b.gguf"
```

### Via API (after server starts)

```bash
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "llama3",
    "file_path": "D:/Models/llama-3-8b-instruct.Q5_K_M.gguf"
  }'
```

Response:
```json
{
  "status": "success",
  "message": "Model loaded: llama3",
  "model_type": "Text LLM",
  "load_time_ms": 2500.5,
  "active": true
}
```

### Verify Model is Loaded

```bash
curl http://localhost:6930/api/v1/models
```

---

## Making Your First Request

### Chat Completion (OpenAI-compatible)

```bash
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "llama3",
    "messages": [
      {"role": "system", "content": "You are a helpful assistant."},
      {"role": "user", "content": "What is the capital of France?"}
    ],
    "max_tokens": 100
  }'
```

Response:
```json
{
  "id": "chatcmpl-abc123",
  "object": "chat.completion",
  "model": "llama3",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "The capital of France is Paris."
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 25,
    "completion_tokens": 8,
    "total_tokens": 33,
    "tokens_per_second": 65.5
  }
}
```

### Streaming Response

```bash
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "llama3",
    "messages": [{"role": "user", "content": "Count from 1 to 5"}],
    "stream": true
  }'
```

---

## Multi-Model Switching

This is SnapLLM's superpower - switch between models in **<1ms**!

### Load Multiple Models

```bash
# Load first model
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"model_id": "general", "file_path": "/models/llama-3-8b.gguf"}'

# Load second model
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"model_id": "coder", "file_path": "/models/codellama-7b.gguf"}'
```

### Switch Models Instantly

```bash
# Switch to coder model (<1ms!)
curl -X POST http://localhost:6930/api/v1/models/switch \
  -H "Content-Type: application/json" \
  -d '{"model_id": "coder"}'
```

### Query the Active Model

```bash
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "coder",
    "messages": [{"role": "user", "content": "Write a bubble sort function in Python"}]
  }'
```

---

## Using the Desktop UI

The SnapLLM Desktop UI provides a beautiful web interface for managing models and chatting.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Your Browser                             │
│                  http://localhost:9780                       │
└─────────────────────┬───────────────────────────────────────┘
                      │ HTTP/REST
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              SnapLLM Desktop UI (React + Vite)              │
│                     Port: 9780                               │
└─────────────────────┬───────────────────────────────────────┘
                      │ HTTP/REST API calls
                      ▼
┌─────────────────────────────────────────────────────────────┐
│              SnapLLM Backend Server (C++)                   │
│                     Port: 6930                               │
│                                                              │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │
│  │ Model A │  │ Model B │  │ Model C │  │   ...   │        │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘        │
└─────────────────────────────────────────────────────────────┘
```

### Prerequisites for UI

- **Node.js 18+** - Download from https://nodejs.org/
- **npm** (comes with Node.js)

Verify installation:
```bash
node --version   # Should show v18.x.x or higher
npm --version    # Should show 9.x.x or higher
```

### Step 1: Start the Backend Server

**Important:** The backend MUST be running before starting the UI.

```bash
# Windows - Option A: Double-click
run_server.bat

# Windows - Option B: Command line
bin\snapllm.exe --server --port 6930

# Windows - Option C: With a model pre-loaded
bin\snapllm.exe --server --port 6930 --load-model mymodel "C:\Models\llama-3-8b.gguf"
```

Wait until you see:
```
================================================================
  SnapLLM HTTP Server v1.0.0
================================================================
  Listening on: http://0.0.0.0:6930
  Workspace:    C:\Users\YourName\SnapLLM_Workspace
  CORS:         enabled
================================================================
```

Verify the backend is running:
```bash
curl http://localhost:6930/health
# Should return: {"status":"ok","version":"1.0.0"...}
```

### Step 2: Start the Desktop UI

Open a **new terminal** (keep the backend running):

```bash
# Navigate to the desktop-app folder
cd desktop-app

# Install dependencies (first time only)
npm install

# Start the development server
npm run dev
```

You should see:
```
  VITE v5.x.x  ready in xxx ms

  ➜  Local:   http://localhost:9780/
  ➜  Network: http://192.168.x.x:9780/
```

### Step 3: Open the UI in Browser

Open your browser and navigate to:

```
http://localhost:9780
```

### UI Pages Overview

| Page | Description | How to Use |
|------|-------------|------------|
| **Dashboard** | System overview, health metrics | View GPU/CPU usage, loaded models |
| **Chat** | Interactive chat interface | Select model, type messages, get responses |
| **Models** | Model management | Load new models, view loaded models, scan folders |
| **Quick Switch** | <1ms model switching | Click to instantly switch between loaded models |
| **Images** | Stable Diffusion generation | Enter prompt, generate images (requires SD model) |
| **Vision** | Multimodal analysis | Upload image, ask questions about it |
| **Contexts** | vPID L2 KV cache | Ingest documents for O(1) RAG queries |
| **A/B Compare** | Side-by-side comparison | Compare responses from 2 models |
| **Playground** | API testing | Test raw API calls |
| **Metrics** | Performance analytics | View tokens/sec, latency, memory usage |

### Quick Workflow: Load Model and Chat

1. **Go to Models page** (sidebar → Models)
2. **Click "Load Model"** button
3. **Fill in the form:**
   - Model ID: `assistant` (any name you want)
   - File Path: `C:\Models\llama-3-8b-instruct.Q5_K_M.gguf` (your actual path)
   - Model Type: `auto`
4. **Click "Load"** and wait for completion
5. **Go to Chat page** (sidebar → Chat)
6. **Select your model** from the dropdown
7. **Type a message** and press Enter
8. **See the response** stream in real-time!

### Quick Workflow: Multi-Model Switching

1. **Load multiple models** via Models page
2. **Go to Quick Switch page** (sidebar → Quick Switch)
3. **Click any model card** to switch instantly (<1ms!)
4. **Go to Chat** and chat with the active model
5. **Switch again** without reloading - that's the SnapLLM magic!

### Running Both as Services (Production)

For a more permanent setup, you can run both as background services:

**Windows (PowerShell):**
```powershell
# Terminal 1: Backend
Start-Process -NoNewWindow -FilePath "bin\snapllm.exe" -ArgumentList "--server --port 6930"

# Terminal 2: Frontend
cd desktop-app
npm run dev
```

**Linux:**
```bash
# Terminal 1: Backend
./bin/snapllm --server --port 6930 &

# Terminal 2: Frontend
cd desktop-app
npm run dev &
```

### Troubleshooting UI Issues

**UI shows "Cannot connect to server"**
- Ensure backend is running on port 6930
- Check: `curl http://localhost:6930/health`
- Verify firewall isn't blocking port 6930

**UI loads but models don't appear**
- Load models via the Models page first
- Or start backend with `--load-model` flag

**Chat doesn't respond**
- Ensure a model is selected in the dropdown
- Check backend terminal for errors
- Verify model loaded successfully

**"npm install" fails**
- Update Node.js to version 18+
- Delete `node_modules` folder and try again
- Run `npm cache clean --force`

**Port 9780 already in use**
- Kill existing process: `npx kill-port 9780`
- Or change port in `desktop-app/vite.config.ts`

---

## CLI Reference

### Complete Command Reference

```bash
snapllm [OPTIONS]
```

### Server Mode Options

| Option | Description | Default |
|--------|-------------|---------|
| `--server` | Start HTTP server mode | - |
| `--port PORT` | Server port | 6930 |
| `--host HOST` | Bind address | 0.0.0.0 |
| `--workspace-root PATH` | Workspace directory | ~/SnapLLM_Workspace |
| `--load-model NAME PATH` | Pre-load model (repeatable) | - |

### Text Generation Options

| Option | Description | Default |
|--------|-------------|---------|
| `--prompt TEXT` | Generate text from prompt | - |
| `--generate TEXT` | Alias for --prompt | - |
| `--max-tokens N` | Maximum tokens to generate | 2000 |
| `--temperature N` | Sampling temperature (0.0-2.0) | 0.8 |
| `--top-p N` | Nucleus sampling (0.0-1.0) | 0.95 |
| `--top-k N` | Top-K sampling | 40 |
| `--repeat-penalty N` | Repetition penalty | 1.1 |
| `--seed N` | Random seed (-1 = random) | -1 |
| `--stream` | Enable streaming output | false |

### Model Management Options

| Option | Description |
|--------|-------------|
| `--load-model NAME PATH` | Load a model with given name and path |
| `--switch-model NAME` | Switch to a loaded model |
| `--list-models` | List all loaded models |
| `--stats` | Show cache statistics |
| `--multi-model-test` | Run switching benchmark |

### GPU Configuration

| Option | Description | Default |
|--------|-------------|---------|
| `--gpu-layers N` | Layers to offload to GPU (-1 = all) | -1 |
| `--vram-budget N` | VRAM budget in MB (0 = auto) | 0 |

### Image Generation Options (Stable Diffusion)

| Option | Description | Default |
|--------|-------------|---------|
| `--load-diffusion NAME PATH` | Load SD model | - |
| `--generate-image PROMPT` | Generate image from prompt | - |
| `--output PATH` | Output image path | output.png |
| `--width N` | Image width | 512 |
| `--height N` | Image height | 512 |
| `--steps N` | Sampling steps | 20 |
| `--cfg-scale N` | CFG guidance scale | 7.0 |
| `--negative PROMPT` | Negative prompt | - |

### Vision/Multimodal Options

| Option | Description |
|--------|-------------|
| `--multimodal MODEL MMPROJ` | Load vision model with projector |
| `--image PATH` | Input image for vision |
| `--vision-prompt TEXT` | Prompt with `<__media__>` marker |

### CLI Examples

#### Server Mode

```bash
# Basic server
bin\snapllm.exe --server

# Server on custom port
bin\snapllm.exe --server --port 8080

# Server with model pre-loaded
bin\snapllm.exe --server --load-model assistant "C:\Models\llama3.gguf"

# Server with multiple models
bin\snapllm.exe --server ^
  --load-model general "C:\Models\llama3.gguf" ^
  --load-model coder "C:\Models\codellama.gguf" ^
  --load-model medical "C:\Models\medicine.gguf"

# Server with custom workspace
bin\snapllm.exe --server --workspace-root "D:\MyWorkspace"
```

#### Direct Text Generation (No Server)

```bash
# Simple generation
bin\snapllm.exe --load-model mymodel "C:\Models\llama3.gguf" ^
  --prompt "Explain quantum computing"

# With sampling parameters
bin\snapllm.exe --load-model mymodel "C:\Models\llama3.gguf" ^
  --prompt "Write a poem about AI" ^
  --temperature 0.9 ^
  --max-tokens 500 ^
  --top-p 0.95

# Streaming output
bin\snapllm.exe --load-model mymodel "C:\Models\llama3.gguf" ^
  --prompt "Count from 1 to 10" ^
  --stream

# Multi-model switching benchmark
bin\snapllm.exe ^
  --load-model a "C:\Models\model1.gguf" ^
  --load-model b "C:\Models\model2.gguf" ^
  --load-model c "C:\Models\model3.gguf" ^
  --multi-model-test
```

#### Image Generation

```bash
# Generate image with Stable Diffusion
bin\snapllm.exe --load-diffusion sd15 "C:\Models\sd-v1-5.ckpt" ^
  --generate-image "A beautiful sunset over mountains" ^
  --output sunset.png ^
  --steps 30 ^
  --cfg-scale 7.5

# With negative prompt
bin\snapllm.exe --load-diffusion sdxl "C:\Models\sdxl.safetensors" ^
  --generate-image "A cute robot" ^
  --negative "blurry, low quality, distorted" ^
  --width 1024 --height 1024 ^
  --output robot.png
```

#### Vision/Multimodal

```bash
# Analyze image with vision model
bin\snapllm.exe ^
  --multimodal "C:\Models\gemma-3-4b.gguf" "C:\Models\mmproj-gemma-3.gguf" ^
  --image "photo.jpg" ^
  --vision-prompt "<__media__> Describe this image in detail" ^
  --max-tokens 500
```

#### Utility Commands

```bash
# Show help
bin\snapllm.exe --help

# List loaded models
bin\snapllm.exe --load-model test "C:\Models\test.gguf" --list-models

# Show cache statistics
bin\snapllm.exe --load-model test "C:\Models\test.gguf" --stats
```

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `SNAPLLM_PORT` | Default server port | 6930 |
| `SNAPLLM_HOST` | Default bind address | 0.0.0.0 |
| `SNAPLLM_WORKSPACE` | Default workspace path | ~/SnapLLM_Workspace |
| `CUDA_VISIBLE_DEVICES` | GPU selection | 0 |

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Invalid arguments |
| 3 | Model load failed |
| 4 | Server startup failed |

---

## Troubleshooting

### Server Won't Start

**Error: CUDA not found**
- Install NVIDIA GPU drivers (525+)
- Ensure CUDA DLLs are in the `bin` folder or PATH

**Error: Port already in use**
- Change port: `--port 8080`
- Or kill the process using port 6930

### Model Won't Load

**Error: File not found**
- Use absolute paths: `D:\Models\model.gguf`
- Escape backslashes in JSON: `D:\\Models\\model.gguf`

**Error: Out of memory**
- Use a smaller quantization (Q4_K_M instead of Q8_0)
- Reduce GPU layers: model loads will use hybrid GPU/CPU

### Slow Performance

**Low tokens/second**
- Ensure CUDA build is being used (not CPU)
- Check GPU utilization with `nvidia-smi`
- Use smaller quantization for faster inference

### Can't Connect from Browser

**CORS errors**
- Server runs with CORS enabled by default
- Check firewall settings

---

## Next Steps

1. **Read the full [README](README.md)** for complete API documentation
2. **Try the [examples](examples/)** for Python integration
3. **Explore [vPID L2 Context Caching](docs/vPID_L2_Context_KV_Cache_Architecture.md)** for RAG use cases
4. **Join the community** and share your feedback!

---

## Getting Help

- **GitHub Issues**: Report bugs and request features
- **Discussions**: Ask questions and share ideas
- **Documentation**: Check `docs/` folder for detailed guides

---

<p align="center">
  <strong>Happy inferencing!</strong>
</p>

<p align="center">
  <em>Developed by</em><br/>
  <a href="https://aroora.ai">
    <img src="logo_files/AROORA_315x91.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>

