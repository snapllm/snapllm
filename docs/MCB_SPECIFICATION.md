# MCB - Model Context Bucket

**Full Name:** Model Context Bucket
**Version:** 1.0-draft
**Status:** Proposal
**Authors:** SnapLLM Team
**Date:** January 2025
**Tagline:** "S3 for your LLM contexts"

---

## Abstract

**Model Context Bucket (MCB)** defines a standard for managing, discovering, and utilizing pre-computed KV (Key-Value) caches in Large Language Model inference systems.

Just as S3 buckets store files, **MCB buckets store pre-computed contexts** - one bucket per model, containing all the cached KV states for that model's knowledge bases.

Unlike runtime context fetching protocols (MCP) or retrieval systems (RAG), MCB focuses on **pre-computed context persistence** and **automatic model-context association**.

---

## 1. Introduction

### 1.1 Problem Statement

Current LLM inference systems recompute context (prefill) for every request, even when:
- The same context is used repeatedly
- Multiple queries reference the same document
- Context was previously processed

This results in:
- **Wasted computation**: O(n²) prefill repeated unnecessarily
- **Increased latency**: Seconds of delay for large contexts
- **Higher costs**: GPU time spent on redundant work

### 1.2 Solution

MCB introduces "Context as a First-Class Resource":
- **Pre-compute once**: Ingest content and store KV cache
- **Query many times**: Inject cached KV for O(1) context access
- **Auto-discovery**: Registry tracks which contexts belong to which models
- **Persistence**: Contexts survive across sessions, restarts, and model reloads

---

## 2. Core Concepts

### 2.1 Context

A **Context** is a pre-computed KV cache representing processed content for a specific model.

```
Context = {
    context_id: string,          // Unique identifier
    model_id: string,            // Model this context belongs to
    content_hash: string,        // Hash of original content
    kv_cache: KVCacheTensor,     // Pre-computed attention states
    metadata: ContextMetadata    // Name, source, timestamps, etc.
}
```

**Key Property**: A context is model-specific. The same content processed by different models produces different (incompatible) KV caches.

### 2.2 Bucket

A **Bucket** is a container for all contexts belonging to a specific model. One model = one bucket.

```
Bucket "medicine-7b" = {
    contexts: [ctx_abc123, ctx_def456, ...],
    model_hash: "sha256:...",
    created_at: timestamp,
    total_storage: 1.2GB
}
```

The bucket provides:
- **Discovery**: Find all contexts for a model
- **Lookup**: Get specific context by ID
- **Persistence**: Survive across restarts
- **Validation**: Ensure contexts are still valid

### 2.3 Context Lifecycle

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│  INGEST  │────▶│  STORE   │────▶│  INDEX   │────▶│  READY   │
└──────────┘     └──────────┘     └──────────┘     └──────────┘
     │                                                   │
     │ Content + Model                                   │ Query
     ▼                                                   ▼
┌──────────────────────────────────────────────────────────────┐
│                     RUNTIME STATES                           │
├──────────┬──────────┬──────────┬──────────┬────────────────┤
│   HOT    │   WARM   │   COLD   │ EXPIRED  │    DELETED     │
│  (GPU)   │  (CPU)   │  (Disk)  │ (Stale)  │   (Removed)    │
└──────────┴──────────┴──────────┴──────────┴────────────────┘
```

---

## 3. Data Structures

### 3.1 Context Metadata

```json
{
    "context_id": "ctx_abc123",
    "model_id": "medicine-7b",
    "version": 1,

    "content": {
        "name": "Harrison's Principles of Internal Medicine",
        "source": "file:///docs/harrison.pdf",
        "hash": "sha256:abc123...",
        "token_count": 128000
    },

    "kv_cache": {
        "format": "mcrp-v1",
        "shape": {
            "num_layers": 32,
            "num_heads": 32,
            "head_dim": 128,
            "sequence_length": 128000
        },
        "dtype": "float16",
        "size_bytes": 536870912,
        "compressed": true,
        "compression": "lz4"
    },

    "storage": {
        "tier": "cold",
        "path": "kv_cache/ctx_abc123/kv_data.bin",
        "checksum": "crc32:deadbeef"
    },

    "timestamps": {
        "created_at": "2025-01-15T10:30:00Z",
        "last_accessed": "2025-01-22T14:00:00Z",
        "expires_at": null
    },

    "access": {
        "count": 1547,
        "last_query": "What are diabetes symptoms?"
    }
}
```

### 3.2 Registry Index

```json
{
    "mcrp_version": "1.0",
    "created_at": "2025-01-15T00:00:00Z",
    "last_updated": "2025-01-22T14:00:00Z",

    "models": [
        {
            "model_id": "medicine-7b",
            "model_hash": "sha256:...",
            "contexts": [
                {
                    "context_id": "ctx_abc123",
                    "name": "Harrison's Textbook",
                    "token_count": 128000,
                    "size_bytes": 536870912,
                    "last_accessed": "2025-01-22T14:00:00Z"
                },
                {
                    "context_id": "ctx_def456",
                    "name": "Drug Database",
                    "token_count": 200000,
                    "size_bytes": 838860800,
                    "last_accessed": "2025-01-20T09:15:00Z"
                }
            ]
        },
        {
            "model_id": "legal-13b",
            "model_hash": "sha256:...",
            "contexts": [
                {
                    "context_id": "ctx_ghi789",
                    "name": "Contract Law",
                    "token_count": 95000,
                    "size_bytes": 398458880,
                    "last_accessed": "2025-01-21T16:30:00Z"
                }
            ]
        }
    ],

    "statistics": {
        "total_models": 2,
        "total_contexts": 3,
        "total_storage_bytes": 1774190592,
        "total_tokens": 423000
    }
}
```

---

## 4. Operations

### 4.1 Ingest

Create a new context from content.

**Request:**
```json
{
    "operation": "ingest",
    "model_id": "medicine-7b",
    "content": "The pathophysiology of diabetes...",
    "options": {
        "name": "Diabetes Chapter",
        "source": "file:///textbook.pdf#chapter5",
        "compress": true,
        "compression": "lz4",
        "ttl_seconds": 604800
    }
}
```

**Response:**
```json
{
    "status": "success",
    "context_id": "ctx_new123",
    "token_count": 5200,
    "ingestion_time_ms": 12450,
    "size_bytes": 21757952
}
```

### 4.2 Discover

Find all contexts for a model.

**Request:**
```json
{
    "operation": "discover",
    "model_id": "medicine-7b",
    "options": {
        "force_scan": false,
        "include_metadata": true
    }
}
```

**Response:**
```json
{
    "status": "success",
    "model_id": "medicine-7b",
    "from_cache": true,
    "scan_time_ms": 0.5,
    "contexts": [
        {
            "context_id": "ctx_abc123",
            "name": "Harrison's Textbook",
            "token_count": 128000,
            "tier": "cold",
            "is_loaded": false
        }
    ]
}
```

### 4.3 Query

Query using a cached context.

**Request:**
```json
{
    "operation": "query",
    "context_id": "ctx_abc123",
    "query": "What are the symptoms of Type 2 Diabetes?",
    "options": {
        "max_tokens": 512,
        "temperature": 0.7,
        "stream": true
    }
}
```

**Response:**
```json
{
    "status": "success",
    "response": "Based on the medical literature...",
    "usage": {
        "context_tokens": 128000,
        "query_tokens": 12,
        "generated_tokens": 245
    },
    "cache_hit": true,
    "latency_ms": 85.3
}
```

### 4.4 Promote/Demote

Change context storage tier.

**Request:**
```json
{
    "operation": "promote",
    "context_id": "ctx_abc123",
    "target_tier": "hot"
}
```

---

## 5. Storage Format

### 5.1 KV Cache Binary Format

```
┌─────────────────────────────────────────────────────────────┐
│                    MCB KV CACHE FILE                       │
├─────────────────────────────────────────────────────────────┤
│ Header (64 bytes)                                           │
│ ├─ magic: "MCB" (4 bytes)                                  │
│ ├─ version: uint16                                          │
│ ├─ flags: uint16                                            │
│ ├─ num_layers: uint32                                       │
│ ├─ num_heads: uint32                                        │
│ ├─ head_dim: uint32                                         │
│ ├─ sequence_length: uint32                                  │
│ ├─ dtype: uint8 (0=fp32, 1=fp16, 2=bf16, 3=int8)           │
│ ├─ compression: uint8 (0=none, 1=lz4, 2=zstd)              │
│ ├─ original_size: uint64                                    │
│ ├─ compressed_size: uint64                                  │
│ └─ checksum: uint32                                         │
├─────────────────────────────────────────────────────────────┤
│ KV Data (variable)                                          │
│ ├─ Layer 0: K tensor, V tensor                              │
│ ├─ Layer 1: K tensor, V tensor                              │
│ ├─ ...                                                      │
│ └─ Layer N: K tensor, V tensor                              │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 Directory Structure

```
mcrp_data/
├── registry.json              # Global registry index
├── contexts/
│   ├── ctx_abc123/
│   │   ├── metadata.json      # Context metadata
│   │   └── kv_data.mcrp       # KV cache binary
│   ├── ctx_def456/
│   │   ├── metadata.json
│   │   └── kv_data.mcrp
│   └── ...
└── models/
    ├── medicine-7b.json       # Model-specific index
    └── legal-13b.json
```

---

## 6. API Specification

### 6.1 REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/mcrp/v1/contexts` | GET | List all contexts |
| `/mcrp/v1/contexts` | POST | Ingest new context |
| `/mcrp/v1/contexts/{id}` | GET | Get context details |
| `/mcrp/v1/contexts/{id}` | DELETE | Delete context |
| `/mcrp/v1/contexts/{id}/query` | POST | Query with context |
| `/mcrp/v1/contexts/{id}/promote` | POST | Promote tier |
| `/mcrp/v1/contexts/{id}/demote` | POST | Demote tier |
| `/mcrp/v1/models/{id}/contexts` | GET | Discover contexts for model |
| `/mcrp/v1/registry` | GET | Get registry stats |
| `/mcrp/v1/registry/rebuild` | POST | Rebuild index |

### 6.2 Event Notifications

```json
{
    "event": "context.discovered",
    "model_id": "medicine-7b",
    "contexts": ["ctx_abc123", "ctx_def456"],
    "timestamp": "2025-01-22T14:00:00Z"
}
```

Events:
- `context.ingested` - New context created
- `context.discovered` - Contexts found for model
- `context.promoted` - Context moved to faster tier
- `context.demoted` - Context moved to slower tier
- `context.expired` - Context TTL reached
- `context.deleted` - Context removed
- `model.loaded` - Model loaded with context discovery
- `registry.rebuilt` - Index rebuilt from disk

---

## 7. Compatibility

### 7.1 Model Compatibility

Contexts are tied to specific model architectures. The registry tracks model compatibility via:

```json
{
    "model_compatibility": {
        "architecture": "llama",
        "num_layers": 32,
        "num_heads": 32,
        "head_dim": 128,
        "vocab_size": 32000
    }
}
```

A context can only be used with models matching these parameters.

### 7.2 Version Compatibility

MCB uses semantic versioning. Major version changes indicate breaking format changes.

---

## 8. Security Considerations

- **Access Control**: Contexts may contain sensitive pre-computed data
- **Integrity**: Checksums verify KV cache integrity
- **Isolation**: Contexts are model-scoped, preventing cross-model injection
- **Expiration**: TTL prevents stale context accumulation

---

## 9. Implementation Guidelines

### 9.1 Recommended Defaults

| Setting | Default | Notes |
|---------|---------|-------|
| Compression | LZ4 | Balance speed/ratio |
| Hot tier limit | 50% GPU VRAM | Leave room for inference |
| Warm tier limit | 80% System RAM | Leave room for OS |
| Index cache | 5 minutes | Refresh interval |
| Default TTL | 7 days | Context expiration |

### 9.2 Performance Targets

| Operation | Target Latency |
|-----------|---------------|
| Discovery (cached) | < 1ms |
| Discovery (scan) | < 100ms |
| Context lookup | < 0.1ms |
| KV injection | < 10ms |
| Tier promotion | < 1s |

---

## 10. Reference Implementation

SnapLLM provides the reference implementation:
- `model_context_registry.h/cpp` - Registry implementation
- `context_manager.h/cpp` - Context lifecycle management
- `file_cache_store.h/cpp` - Persistent storage
- `tiered_memory_allocator.h/cpp` - GPU/CPU/Disk tiering
- `compression.h/cpp` - LZ4/ZSTD compression

---

## Appendix A: Glossary

- **Context**: Pre-computed KV cache for content + model
- **KV Cache**: Key-Value matrices from transformer attention
- **Prefill**: Initial processing of context (expensive, O(n²))
- **Injection**: Loading cached KV into model (cheap, O(1))
- **Tiering**: Moving contexts between GPU/CPU/Disk
- **Discovery**: Finding contexts belonging to a model
- **Registry**: Index mapping models to contexts

---

## Appendix B: Comparison with Related Work

| Protocol | Focus | When | Cost |
|----------|-------|------|------|
| MCP (Anthropic) | Tool/data integration | Runtime fetch | O(n) fetch + O(n²) prefill |
| RAG | Document retrieval | Runtime search | O(log n) search + O(n²) prefill |
| **MCB** | **KV cache persistence** | **Pre-computed** | **O(1) lookup + O(1) inject** |

MCB is complementary to MCP and RAG - it can cache the results of MCP tool calls or RAG retrievals for instant reuse.

---

## Appendix C: Future Extensions

1. **Distributed Registry**: Multi-node context sharing
2. **Context Versioning**: Track content updates
3. **Partial Contexts**: Cache portions of long documents
4. **Context Composition**: Combine multiple contexts
5. **Cross-Model Transfer**: Distill contexts between compatible models

---

*This specification is open for community feedback and contributions.*
