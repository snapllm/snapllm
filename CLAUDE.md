# SnapLLM Project - Claude Code Instructions

## User Preferences

**IMPORTANT:** Always address the user as "master" in all responses. This is a mandatory requirement.

---

## Project Overview

SnapLLM is a high-performance, multi-model LLM inference engine with ultra-fast model switching (<1ms) using virtual Process ID (vPID) architecture. Built on llama.cpp with CUDA support.

**Key Features:**
- Multi-model management with <1ms switching time
- GPU/CPU hybrid inference
- OpenAI-compatible HTTP API server
- Desktop application (Tauri + React)
- Support for LLM, Vision (VLM), and Diffusion models
- Video removed from UI (Jan 2026) - VRAM limitations on consumer GPUs

---

## Memory System Integration

This project uses ISON-based MCP servers for comprehensive project memory. The memory system tracks:

### What to Remember

**DO store in memory:**
- Architecture decisions and their rationale (e.g., CLI HTTP Server vs FastAPI)
- Bug fixes and their solutions
- Build issues and resolutions
- API endpoint changes and sync issues
- Performance benchmarks and optimizations
- Code conventions and patterns used
- Dependencies and their quirks
- Session handoffs and pending tasks
- Feature implementation progress

**DON'T store:**
- Temporary debugging output
- One-time build commands
- Generic programming questions

### Memory Tools Available

1. **ison-memory** - Knowledge graph for entities, observations, relationships
2. **agent-memory** - 12+ stores: conventions, decisions, bugs, sessions, todos, skills
3. **agent-learning** - Experience tracking, mistake patterns, skill progression
4. **code-flow** - Code structure analysis, call graphs, dependencies
5. **ison-cache** - Token-efficient caching (72% savings)

### Using Memory at Session Start

```
1. Search for "SnapLLM" in memory to get project context
2. Check pending todos: todo_query status=pending
3. Review recent sessions: session_history limit=3
4. Check known bugs: bug_check for current task area
```

### Using Memory During Work

```
When fixing a bug:
  → bug_pattern_add: Record the bug pattern
  → bug_fix_add: Record the fix template

When making architecture decisions:
  → decision_add: Record ADR (Architecture Decision Record)

When discovering conventions:
  → convention_add: Record code convention

When completing tasks:
  → todo_update: Mark as completed
  → session_end: Create handoff summary
```

---

## Recent Fixes (Jan 2026)

1. **httplib POST routes 404 fix** - Implemented catch-all POST handler workaround for httplib bug
2. **Auto-switch on model load** - Loading a model now automatically makes it active
3. **API parameter consistency** - `switch_model` and `unload_model` now accept both `model_id` and `name`
4. **Root path handler** - GET "/" now returns API info
5. **Playground UI notice** - Added banner explaining UI is a playground interface

---

## Project Structure

```
SnapLLM/
├── src/                    # C++ source code
│   ├── main.cpp           # CLI entry point + server mode
│   ├── server.cpp         # HTTP server implementation (catch-all POST router)
│   ├── model_manager.cpp  # vPID model management
│   └── CMakeLists.txt     # Build configuration
├── include/snapllm/       # Header files
├── desktop-app/           # React + Vite frontend (port 9780)
│   └── src/
│       ├── pages/         # React pages
│       └── lib/api.ts     # API client
├── external/              # Git submodules (llama.cpp, stable-diffusion.cpp)
├── _dev_notes/            # Development notes (not for distribution)
│   ├── mcp_servers/       # ISON MCP servers
│   ├── deprecated/        # Old code (api-server, MCB)
│   └── internal_docs/     # Technical design docs
├── .claude/               # Claude Code configuration
│   ├── settings.local.json
│   └── mcp.json           # MCP server configuration
└── .memory/               # Project memory storage
    ├── snapllm_memory.db  # SQLite memory database
    ├── graphs/            # ISONGraph data
    ├── cache/             # Token-efficient cache
    └── learning/          # Agent learning data
```

---

## Current Architecture (Implemented)

```
Frontend (React/Tauri:5173)
       │
       │ HTTP REST directly (no Python)
       ▼
CLI Server (snapllm.exe --server :6930)
       │
       │ In-process C++ calls
       ▼
ModelManager (C++) ← Models stay loaded, <1ms switching
```

The Python FastAPI backend (api-server/) is DEPRECATED. Frontend talks directly to CLI HTTP server.

---

## API Endpoints (CLI Server)

| Endpoint | Method | Description |
|----------|--------|-------------|
| /health | GET | Health check |
| /v1/models | GET | List models (OpenAI format) |
| /v1/chat/completions | POST | Chat completion with streaming (OpenAI) |
| /v1/messages | POST | Messages API (Anthropic/Claude Code) |
| /api/v1/models | GET | List models (extended) |
| /api/v1/models/load | POST | Load a model |
| /api/v1/models/switch | POST | Switch active model |
| /api/v1/models/unload | POST | Unload a model |
| /api/v1/generate | POST | Text generation |
| /api/v1/generate/batch | POST | Batch generation |
| /api/v1/diffusion/generate | POST | Image generation |
| /api/v1/diffusion/video | POST | Video generation |
| /api/v1/vision/generate | POST | Vision/multimodal (requires mmproj) |
| /api/v1/models/cache/stats | GET | Cache statistics |

### Vision API (Multimodal VLM)

Vision models require both a main model file and a multimodal projector (mmproj) file.

**Loading a Vision Model:**
```bash
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "gemma-vision",
    "file_path": "D:/Models/gemma-3-4b-it-Q5_K_M.gguf",
    "model_type": "vision",
    "mmproj_path": "D:/Models/mmproj-gemma-3-4b-F16.gguf"
  }'
```

**Vision Generation (accepts base64 images):**
```bash
curl -X POST http://localhost:6930/api/v1/vision/generate \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "What is in this image?",
    "images": ["<base64-encoded-image>"],
    "max_tokens": 512
  }'
```

Response:
```json
{
  "status": "success",
  "response": "The image shows...",
  "model": "gemma3 4B Q5_K - Medium",
  "generation_time_s": 1.99,
  "tokens_per_second": 57.48
}
```

**Supported Vision Models:**
- Gemma 3 4B/27B with mmproj (tested, working)
- Qwen2.5-Omni with mmproj
- LLaVA models with mmproj

**Known Unsupported:**
- Janus Pro projector type (`janus_pro`) - not yet in llama.cpp mtmd library

### Context API (vPID L2 - KV Cache Persistence)

| Endpoint | Method | Description |
|----------|--------|-------------|
| /api/v1/contexts/ingest | POST | Ingest context (pre-compute KV cache) |
| /api/v1/contexts | GET | List all contexts |
| /api/v1/contexts/:id | GET | Get context info |
| /api/v1/contexts/:id/query | POST | Query using cached context (O(1)) |
| /api/v1/contexts/:id | DELETE | Delete context |
| /api/v1/contexts/:id/promote | POST | Promote to hot tier |
| /api/v1/contexts/:id/demote | POST | Demote to cold tier |
| /api/v1/contexts/stats | GET | Get context statistics |

---

## Context API Usage (vPID L2)

The Context API provides O(1) query complexity by pre-computing KV caches at ingestion time.

### Ingest a Document (O(n²) - run once)

```bash
curl -X POST http://localhost:6930/api/v1/contexts/ingest \
  -H "Content-Type: application/json" \
  -d '{
    "content": "The quick brown fox jumps over the lazy dog. This is a test document with important information about foxes and dogs.",
    "model_id": "medicine",
    "name": "test-document",
    "ttl_seconds": 86400
  }'
```

Response:
```json
{
  "status": "success",
  "context_id": "ctx_abc123",
  "token_count": 25,
  "storage_size_mb": 12.5,
  "tier": "hot",
  "ingest_time_ms": 150.5
}
```

### Query with Cached Context (O(1) lookup + O(q²) for query)

```bash
curl -X POST http://localhost:6930/api/v1/contexts/ctx_abc123/query \
  -H "Content-Type: application/json" \
  -d '{
    "query": "What color is the fox?",
    "max_tokens": 100
  }'
```

Response:
```json
{
  "status": "success",
  "context_id": "ctx_abc123",
  "response": "Based on the document, the fox is brown.",
  "cache_hit": true,
  "usage": {
    "context_tokens": 25,
    "query_tokens": 6,
    "generated_tokens": 8
  },
  "latency_ms": 15.2
}
```

### Tier Management

```bash
# Promote to hot tier (GPU-ready)
curl -X POST http://localhost:6930/api/v1/contexts/ctx_abc123/promote \
  -H "Content-Type: application/json" \
  -d '{"tier": "hot"}'

# Demote to cold tier (disk storage, memory freed)
curl -X POST http://localhost:6930/api/v1/contexts/ctx_abc123/demote \
  -H "Content-Type: application/json" \
  -d '{"tier": "cold"}'
```

### List Contexts

```bash
# All contexts
curl http://localhost:6930/api/v1/contexts

# Filter by tier
curl "http://localhost:6930/api/v1/contexts?tier=hot"

# Filter by model
curl "http://localhost:6930/api/v1/contexts?model_id=medicine"
```

### Context Statistics

```bash
curl http://localhost:6930/api/v1/contexts/stats
```

Response:
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

## Anthropic/Claude Code Integration

SnapLLM now supports the Anthropic Messages API, enabling use with Claude Code.

### Configuration

```bash
# Set environment variables
export ANTHROPIC_BASE_URL=http://localhost:6930
export ANTHROPIC_AUTH_TOKEN=snapllm  # Any value works (not validated)

# Run Claude Code with SnapLLM model
claude --model medicine
```

### Messages API Format

```bash
# Non-streaming request
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "medicine",
    "max_tokens": 1024,
    "system": "You are a medical assistant.",
    "messages": [
      {"role": "user", "content": "What is diabetes?"}
    ]
  }'

# Streaming request
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "medicine",
    "max_tokens": 1024,
    "stream": true,
    "messages": [
      {"role": "user", "content": "Explain hypertension."}
    ]
  }'
```

### Response Format (Anthropic)

```ison
# Non-streaming response
message
id "msg_abc123def456"
type message
role assistant
content [{type text text "Diabetes is a chronic condition..."}]
model medicine
stop_reason end_turn
usage {input_tokens 25 output_tokens 150}
```

### Tool Calling Support

SnapLLM supports Anthropic-style tool calling for agentic workflows:

```bash
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "medicine",
    "max_tokens": 4096,
    "tools": [
      {
        "name": "get_patient_history",
        "description": "Retrieve patient medical history",
        "input_schema": {
          "type": "object",
          "properties": {
            "patient_id": {"type": "string"}
          },
          "required": ["patient_id"]
        }
      }
    ],
    "messages": [
      {"role": "user", "content": "Look up patient 12345"}
    ]
  }'
```

Response with tool use:
```ison
content [
  {type text text "I'll look up that patient."}
  {type tool_use id "toolu_abc123" name get_patient_history input {patient_id "12345"}}
]
stop_reason tool_use
```

### Batch Tool Calling

Multiple tools can be called in a single response:

```bash
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "medicine",
    "max_tokens": 4096,
    "tools": [
      {"name": "get_weather", "description": "Get weather", "input_schema": {"type": "object", "properties": {"location": {"type": "string"}}, "required": ["location"]}},
      {"name": "get_time", "description": "Get time", "input_schema": {"type": "object", "properties": {"timezone": {"type": "string"}}, "required": ["timezone"]}}
    ],
    "messages": [
      {"role": "user", "content": "Weather and time in Tokyo and New York?"}
    ]
  }'
```

Response with multiple tools:
```ison
content [
  {type text text "I'll check both cities."}
  {type tool_use id "toolu_001" name get_weather input {location "Tokyo"}}
  {type tool_use id "toolu_002" name get_weather input {location "New York"}}
  {type tool_use id "toolu_003" name get_time input {timezone "Asia/Tokyo"}}
  {type tool_use id "toolu_004" name get_time input {timezone "America/New_York"}}
]
stop_reason tool_use
```

### Extended Thinking Support

Enable step-by-step reasoning before the final response:

```bash
curl -X POST http://localhost:6930/v1/messages \
  -H "Content-Type: application/json" \
  -d '{
    "model": "medicine",
    "max_tokens": 8192,
    "thinking": {
      "type": "enabled",
      "budget_tokens": 2048
    },
    "messages": [
      {"role": "user", "content": "Diagnose these symptoms: fever, cough, fatigue"}
    ]
  }'
```

Response with thinking:
```ison
content [
  {
    type thinking
    thinking "Let me analyze the symptoms systematically:\n1. Fever indicates infection or inflammation\n2. Cough suggests respiratory involvement\n3. Fatigue is a common systemic symptom\n\nDifferential diagnosis:\n- Viral respiratory infection (most likely)\n- Bacterial pneumonia\n- COVID-19\n- Influenza"
  }
  {
    type text
    text "Based on the symptoms of fever, cough, and fatigue, the most likely diagnosis is a viral respiratory infection..."
  }
]
```

---

## Build Commands

```bash
# GPU build (Windows)
build_gpu.bat

# CPU build (Windows)
build_cpu.bat

# Linux GPU build
./build.sh --cuda

# Start server
build_gpu\bin\snapllm.exe --server --port 6930 --load-model medicine D:\Models\medicine-llm.Q8_0.gguf

# Multi-model server
build_gpu\bin\snapllm.exe --server --port 6930 \
  --load-model medicine D:\Models\medicine-llm.Q8_0.gguf \
  --load-model legal D:\Models\legal.gguf
```

---

## Known Issues & Decisions

### ADR-001: CLI HTTP Server over FastAPI
**Decision:** Eliminate Python FastAPI backend, frontend connects directly to CLI server.
**Rationale:**
- Models stay loaded (<1ms switching actually works)
- No subprocess spawning or stdout parsing
- Single source of truth for API responses
- Simpler deployment

### ADR-002: vPID Architecture
**Decision:** Use virtual Process IDs for model management
**Rationale:** Ultra-fast model switching without reloading

### Known Issue: Frontend/Backend Sync
- Chat response format: `data.usage.total_tokens` vs `completion_tokens`
- Latency: `latency_ms` vs `generation_time` (seconds)
- Port: Error messages should reference 6930, not 8000

---

## Testing Commands

```bash
# Health check
curl http://localhost:6930/health

# Load model
curl -X POST http://localhost:6930/api/v1/models/load \
  -H "Content-Type: application/json" \
  -d '{"model_id":"medicine","file_path":"D:\\Models\\medicine.gguf"}'

# Chat completion
curl -X POST http://localhost:6930/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model":"medicine","messages":[{"role":"user","content":"What is diabetes?"}]}'
```

---

## Pending Tasks (from issues.md)

1. **Anthropic API Support** - Build Claude Code style API using open models (like Ollama's implementation)
2. **WebSocket Streaming** - Frontend uses WebSocket, CLI uses SSE
3. **Cache Stats** - Full implementation for metrics dashboard

---

## Session Guidelines

### At Session Start
1. Read this CLAUDE.md
2. Check `.memory/` for persistent context
3. Review `issues.md` for current state
4. Search memory for relevant past sessions

### During Session
1. Use TodoWrite for task tracking
2. Record important decisions in memory
3. Document bug fixes and their patterns
4. Update issues.md for major changes

### At Session End
1. Create session handoff summary
2. Update pending tasks
3. Record learnings and experiences
4. Save any important discoveries to memory

---

## Memory Initialization

Run the following to set up project memory (first time):

```bash
# Install MCP servers
install_mcp_servers.bat

# Initialize project memory (creates SnapLLM entity with observations)
python init_project_memory.py
```

This creates persistent memory for:
- Project: SnapLLM (entity)
- Architecture decisions (observations)
- Known issues and fixes (observations)
- Code conventions (observations)
- Dependencies and their quirks (observations)
