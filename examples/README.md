# SnapLLM Examples

This directory contains example scripts demonstrating how to use SnapLLM's features.

## Prerequisites

```bash
pip install requests
```

Make sure the SnapLLM server is running:

```bash
# Windows
build_gpu\bin\snapllm.exe --server --port 6930

# Linux
./build_gpu/bin/snapllm --server --port 6930
```

## Examples

### 1. Basic Usage (`basic_usage.py`)

Demonstrates fundamental operations:
- Loading models
- Chat completion
- Model switching
- Unloading models

```bash
python basic_usage.py
```

### 2. Multi-Model Switching (`multi_model_switching.py`)

Showcases SnapLLM's key feature - ultra-fast (<1ms) model switching:
- Benchmark switching times
- Multi-domain assistant
- A/B model comparison
- Rapid ensemble queries

```bash
python multi_model_switching.py
```

### 3. Context Caching (`context_caching.py`)

Demonstrates vPID L2 context caching for efficient RAG:
- Document ingestion (one-time KV cache computation)
- O(1) context queries
- Tier management (hot/warm/cold)
- Cache statistics

```bash
python context_caching.py
```

## Configuration

Update the model paths in each script to point to your actual GGUF model files:

```python
MODEL_PATHS = {
    "general": "/path/to/your/model.gguf",
    # ...
}
```

## API Reference

All examples use the SnapLLM HTTP API. See the main README for complete API documentation.

### Quick Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/v1/models/load` | POST | Load a model |
| `/api/v1/models/switch` | POST | Switch active model (<1ms) |
| `/v1/chat/completions` | POST | Chat completion (OpenAI format) |
| `/api/v1/contexts/ingest` | POST | Ingest document for caching |
| `/api/v1/contexts/{id}/query` | POST | Query cached context |
