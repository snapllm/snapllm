# vPID L2 Implementation Strategy

## Separation of Concerns, Architectural Evolution & Lessons Learned

**Document Version:** 1.0
**Date:** January 2026
**Status:** Implementation Guide

---

## Table of Contents

1. [Introduction](#introduction)
2. [Current Architecture Analysis](#current-architecture-analysis)
3. [Lessons Learned from Current Architecture](#lessons-learned-from-current-architecture)
4. [Separation of Concerns Framework](#separation-of-concerns-framework)
5. [Deterministic Versioning Strategy](#deterministic-versioning-strategy)
6. [Implementation Phases](#implementation-phases)
7. [Module Architecture & Boundaries](#module-architecture--boundaries)
8. [Interface Contracts](#interface-contracts)
9. [Migration Strategy](#migration-strategy)
10. [Testing Strategy](#testing-strategy)
11. [Risk Analysis & Mitigation](#risk-analysis--mitigation)
12. [Future Architecture Roadmap](#future-architecture-roadmap)
13. [Appendix: Decision Records](#appendix-decision-records)

---

## Introduction

### Purpose

This document provides a comprehensive implementation strategy for extending SnapLLM's vPID architecture from Level 1 (model-level caching) to Level 2 (context-level KV caching). It emphasizes:

1. **Separation of Concerns** - Clean module boundaries
2. **Deterministic Evolution** - Predictable, versioned changes
3. **Lessons Learned** - Applying insights from current architecture
4. **Future-Proofing** - Designing for L3 and beyond

### Guiding Principles

```
┌─────────────────────────────────────────────────────────────────┐
│                    ARCHITECTURAL PRINCIPLES                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. SEPARATION: Each layer owns ONE responsibility             │
│  2. ISOLATION: Failures don't cascade across boundaries        │
│  3. EVOLUTION: New features don't break existing ones          │
│  4. DETERMINISM: Same inputs → Same outputs, always            │
│  5. OBSERVABILITY: Every operation is traceable                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Current Architecture Analysis

### What We Have Today (vPID L1)

```
┌─────────────────────────────────────────────────────────────────┐
│                    CURRENT SNAPLLM ARCHITECTURE                 │
│                         (vPID Level 1)                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    HTTP Server Layer                      │  │
│  │  server.cpp - httplib-based, handles routing              │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    API Handler Layer                      │  │
│  │  OpenAI endpoints (/v1/*), Anthropic (/v1/messages)      │  │
│  │  Custom endpoints (/api/v1/*)                            │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Model Manager (vPID L1)                │  │
│  │  model_manager.cpp - Multi-model lifecycle               │  │
│  │  - Model loading/unloading                               │  │
│  │  - vPID assignment                                       │  │
│  │  - <1ms model switching                                  │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    llama.cpp Integration                  │  │
│  │  Inference engine, KV cache (session-scoped)             │  │
│  └──────────────────────────────────────────────────────────┘  │
│                              │                                  │
│                              ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Hardware Layer                         │  │
│  │  CUDA/CPU backends, memory management                    │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Current Component Responsibilities

| Component | Responsibility | Coupling Level |
|-----------|---------------|----------------|
| server.cpp | HTTP routing, request/response | Medium (knows about handlers) |
| API Handlers | Protocol translation, validation | High (knows about models) |
| ModelManager | Model lifecycle, vPID mapping | High (knows about inference) |
| llama.cpp | Inference, KV cache | Low (self-contained) |

### Current Data Flow

```
Request → Server → Handler → ModelManager → llama.cpp → Response
                                    │
                                    ▼
                            [KV Cache - Session Only]
                            [Discarded after request]
```

### Identified Coupling Issues

```
┌─────────────────────────────────────────────────────────────────┐
│                    CURRENT COUPLING ANALYSIS                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  PROBLEM 1: Handler ↔ ModelManager Tight Coupling               │
│  ─────────────────────────────────────────────────              │
│  Handlers directly call ModelManager methods                    │
│  Adding new resource types (contexts) requires handler changes  │
│                                                                 │
│  PROBLEM 2: KV Cache Embedded in llama.cpp                      │
│  ─────────────────────────────────────────────────              │
│  No abstraction for cache management                            │
│  Can't persist or share KV across requests                      │
│                                                                 │
│  PROBLEM 3: No Resource Abstraction                             │
│  ─────────────────────────────────────────────────              │
│  Models are the only "resource" type                            │
│  No unified resource management pattern                         │
│                                                                 │
│  PROBLEM 4: Memory Management is Implicit                       │
│  ─────────────────────────────────────────────────              │
│  No explicit memory budgeting                                   │
│  No tiering or eviction policies                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Lessons Learned from Current Architecture

### Lesson 1: Subprocess Architecture is Fragile

**Original Approach (Deprecated):**
```
Python FastAPI → subprocess(snapllm.exe) → Parse stdout → Response
```

**Problems Encountered:**
- Model reloaded on every request
- Stdout parsing is error-prone
- No state persistence between calls
- Translation layer introduced bugs

**Learning Applied:**
```
✓ Direct HTTP server in C++ (httplib)
✓ Models stay loaded (vPID L1)
✓ Single source of truth for responses
✓ No subprocess, no translation layer
```

**Future Application (L2):**
```
Same principle: KV cache stays loaded, no recomputation
Direct access to cached data, no serialization overhead
```

---

### Lesson 2: Protocol Compatibility Matters

**Original Approach:**
```
Custom API only → Limited tool compatibility
```

**Evolution:**
```
Added OpenAI-compatible endpoints → Broader adoption
Added Anthropic Messages API → Claude Code integration
```

**Learning Applied:**
```
✓ /v1/chat/completions (OpenAI)
✓ /v1/messages (Anthropic)
✓ /api/v1/* (Custom extensions)
```

**Future Application (L2):**
```
Extend existing endpoints with context_id parameter
Don't create entirely new protocols
Maintain backwards compatibility
```

---

### Lesson 3: Response Format Consistency is Critical

**Bug Encountered:**
```
Frontend expected: data.usage.total_tokens
Backend returned: data.completion_tokens

Frontend expected: latency_ms
Backend returned: generation_time (seconds)
```

**Root Cause:**
- No single source of truth for response schemas
- Multiple code paths returning different formats

**Learning Applied:**
```
✓ Standardized response structures
✓ Schema validation at boundaries
```

**Future Application (L2):**
```
Define context response schemas upfront
Single ResponseBuilder for all context operations
Schema tests in CI
```

---

### Lesson 4: Separation Enables Evolution

**vPID L1 Success:**
```
ModelManager separated from inference engine
→ Could add multi-model support without changing inference
→ Could add model switching without changing API
```

**Learning:**
```
Clean interfaces enable independent evolution
Each layer can be improved without touching others
```

**Future Application (L2):**
```
ContextManager separated from ModelManager
→ Can add context features without touching model code
→ Can evolve caching strategies independently
```

---

### Lesson 5: Memory Management Needs Explicit Strategy

**Current State:**
```
Models loaded → GPU memory consumed
No eviction policy → Manual unload required
No visibility → Users don't know memory state
```

**Problems:**
- OOM errors with multiple models
- No automatic resource management
- Manual intervention required

**Future Application (L2):**
```
Explicit memory budgeting
Automatic tiering (GPU → CPU → SSD)
LRU eviction with configurable policies
Memory stats exposed via API
```

---

### Lesson 6: Streaming Architecture is Essential

**Evolution:**
```
v1: No streaming → Users wait for full response
v2: SSE streaming → Better UX for chat
v3: WebSocket + SSE → Bidirectional for tools
```

**Learning:**
```
Design for streaming from the start
Chunked processing enables better UX
Backpressure handling is critical
```

**Future Application (L2):**
```
Streaming ingestion for large documents
Progress events during KV computation
Streaming queries with cached context
```

---

### Consolidated Lessons Matrix

| Lesson | Current Impact | L2 Application | L3 Consideration |
|--------|---------------|----------------|------------------|
| No subprocess | <1ms model switch | Direct cache access | Distributed coordination |
| Protocol compat | Tool ecosystem | Extend, don't replace | Cross-node protocol |
| Schema consistency | Fewer bugs | Context schemas | Distributed schemas |
| Separation | Model evolution | Context isolation | Service boundaries |
| Memory strategy | Manual management | Auto tiering | Distributed memory |
| Streaming | Better UX | Progress tracking | Async replication |

---

## Separation of Concerns Framework

### Architectural Layers (Current + L2)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    SNAPLLM LAYERED ARCHITECTURE                         │
│                    (Current + vPID L2 Extension)                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 6: PRESENTATION (HTTP/WebSocket)                            │ │
│  │ Responsibility: Protocol handling, serialization                  │ │
│  │ Components: server.cpp, websocket.cpp                             │ │
│  │ Dependencies: Layer 5                                             │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                    │                                    │
│                                    ▼                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 5: API (Request Handling)                                   │ │
│  │ Responsibility: Validation, routing, response formatting          │ │
│  │ Components: openai_handler.cpp, anthropic_handler.cpp,            │ │
│  │             context_handler.cpp [NEW]                             │ │
│  │ Dependencies: Layer 4                                             │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                    │                                    │
│                                    ▼                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 4: ORCHESTRATION (vPID Controller)                          │ │
│  │ Responsibility: Coordinate resources, transactions                │ │
│  │ Components: vpid_controller.cpp [NEW]                             │ │
│  │ Dependencies: Layer 3                                             │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                    │                                    │
│                         ┌─────────┴─────────┐                          │
│                         ▼                   ▼                          │
│  ┌─────────────────────────────┐ ┌─────────────────────────────┐      │
│  │ LAYER 3A: MODEL MANAGER     │ │ LAYER 3B: CONTEXT MANAGER   │      │
│  │ (vPID L1)                   │ │ (vPID L2) [NEW]             │      │
│  │ Responsibility:             │ │ Responsibility:             │      │
│  │ - Model lifecycle           │ │ - Context lifecycle         │      │
│  │ - Model switching           │ │ - KV cache management       │      │
│  │ - Model registry            │ │ - Context registry          │      │
│  │ Components:                 │ │ Components:                 │      │
│  │   model_manager.cpp         │ │   context_manager.cpp       │      │
│  │ Dependencies: Layer 2       │ │ Dependencies: Layer 2       │      │
│  └─────────────────────────────┘ └─────────────────────────────┘      │
│                         │                   │                          │
│                         └─────────┬─────────┘                          │
│                                   ▼                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 2: MEMORY MANAGEMENT                                        │ │
│  │ Responsibility: Unified memory allocation, tiering, eviction      │ │
│  │ Components: memory_manager.cpp [NEW], cache_store.cpp [NEW]       │ │
│  │ Dependencies: Layer 1                                             │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                    │                                    │
│                                    ▼                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 1: INFERENCE ENGINE                                         │ │
│  │ Responsibility: Model inference, attention computation            │ │
│  │ Components: llama.cpp integration, kv_compute.cpp [NEW]           │ │
│  │ Dependencies: Layer 0                                             │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                    │                                    │
│                                    ▼                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │ LAYER 0: HARDWARE ABSTRACTION                                     │ │
│  │ Responsibility: GPU/CPU operations, memory allocation             │ │
│  │ Components: cuda_backend.cpp, cpu_backend.cpp                     │ │
│  │ Dependencies: Hardware                                            │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### Separation Rules

```
┌─────────────────────────────────────────────────────────────────┐
│                    SEPARATION RULES                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  RULE 1: Downward Dependencies Only                            │
│  ─────────────────────────────────                             │
│  Layer N can only depend on Layer N-1                          │
│  NO upward dependencies allowed                                │
│  NO lateral dependencies (3A ↔ 3B forbidden directly)          │
│                                                                 │
│  RULE 2: Interface Contracts                                    │
│  ─────────────────────────────                                 │
│  Each layer exposes a stable interface                         │
│  Implementation details are hidden                              │
│  Changes within layer don't affect consumers                   │
│                                                                 │
│  RULE 3: Single Responsibility                                  │
│  ─────────────────────────────                                 │
│  Each component has ONE reason to change                       │
│  Model changes don't affect Context code                       │
│  Memory changes don't affect API code                          │
│                                                                 │
│  RULE 4: Data Ownership                                         │
│  ────────────────────────                                      │
│  Each layer owns its data structures                           │
│  Data crosses boundaries via defined DTOs                      │
│  No shared mutable state between layers                        │
│                                                                 │
│  RULE 5: Error Boundaries                                       │
│  ───────────────────────                                       │
│  Errors are caught at layer boundaries                         │
│  Translated to appropriate abstraction level                   │
│  Lower layer errors don't leak implementation details          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Component Ownership Matrix

| Component | Owner Layer | Creates | Consumes | Modifies |
|-----------|-------------|---------|----------|----------|
| HTTP Request | L6 | ✓ | - | - |
| API Response | L5 | ✓ | - | - |
| Transaction | L4 | ✓ | - | ✓ |
| Model Handle | L3A | ✓ | L4 | L3A |
| Context Handle | L3B | ✓ | L4 | L3B |
| Memory Block | L2 | ✓ | L3A, L3B | L2 |
| KV Tensor | L1 | ✓ | L2 | L1 |
| GPU Buffer | L0 | ✓ | L1 | L0 |

---

## Deterministic Versioning Strategy

### Version Numbering

```
SNAPLLM_VERSION = MAJOR.MINOR.PATCH-VPID_LEVEL

Examples:
  2.0.0-L1  → vPID Level 1 (current)
  2.1.0-L1  → L1 with minor features
  3.0.0-L2  → vPID Level 2 introduced
  3.1.0-L2  → L2 with improvements
  4.0.0-L3  → vPID Level 3 introduced
```

### API Versioning

```
/v1/*           → Stable, OpenAI/Anthropic compatible
/v2/*           → vPID L2 native endpoints (future)
/api/v1/*       → Custom stable endpoints
/api/v2/*       → L2 context endpoints
/api/internal/* → Unstable, internal use
```

### Compatibility Matrix

| Client Version | Server 2.x (L1) | Server 3.x (L2) | Server 4.x (L3) |
|---------------|-----------------|-----------------|-----------------|
| v1 API | ✓ Full | ✓ Full | ✓ Full |
| v2 API (L2) | ✗ N/A | ✓ Full | ✓ Full |
| v3 API (L3) | ✗ N/A | ✗ N/A | ✓ Full |

### Feature Flags

```cpp
// Compile-time feature flags
#define SNAPLLM_VPID_L1 1  // Always enabled
#define SNAPLLM_VPID_L2 1  // Enable L2 features
#define SNAPLLM_VPID_L3 0  // Future

// Runtime feature detection
struct SnapLLMCapabilities {
    bool vpid_l1 = true;
    bool vpid_l2 = SNAPLLM_VPID_L2;
    bool vpid_l3 = SNAPLLM_VPID_L3;

    // Fine-grained L2 features
    bool context_caching = SNAPLLM_VPID_L2;
    bool memory_tiering = SNAPLLM_VPID_L2;
    bool hierarchical_context = SNAPLLM_VPID_L2;
};

// Expose via API
// GET /api/v1/capabilities
{
    "vpid_levels": ["L1", "L2"],
    "features": {
        "model_switching": true,
        "context_caching": true,
        "memory_tiering": true
    }
}
```

### Deprecation Policy

```
┌─────────────────────────────────────────────────────────────────┐
│                    DEPRECATION TIMELINE                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Phase 1: Announcement (Version N)                              │
│  ─────────────────────────────────                             │
│  - Add deprecation warning to docs                             │
│  - Add runtime warning (logged, not returned)                  │
│  - Provide migration guide                                     │
│                                                                 │
│  Phase 2: Soft Deprecation (Version N+1)                        │
│  ─────────────────────────────────────                         │
│  - Add deprecation header in responses                         │
│  - Feature still works                                         │
│  - Metrics track deprecated feature usage                      │
│                                                                 │
│  Phase 3: Hard Deprecation (Version N+2)                        │
│  ────────────────────────────────────                          │
│  - Feature disabled by default                                 │
│  - Can be re-enabled via config                                │
│  - Clear migration required message                            │
│                                                                 │
│  Phase 4: Removal (Version N+3)                                 │
│  ─────────────────────────────                                 │
│  - Feature removed from codebase                               │
│  - Returns 410 Gone for API endpoints                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    IMPLEMENTATION PHASES                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PHASE 0: FOUNDATION (Preparation)                               │   │
│  │ ───────────────────────────────────                             │   │
│  │ • Define interfaces and contracts                               │   │
│  │ • Set up testing infrastructure                                 │   │
│  │ • Create memory management abstraction                          │   │
│  │ • Refactor existing code for separation                         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│                                    ▼                                    │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PHASE 1: CORE CACHE (Single-Tier MVP)                           │   │
│  │ ─────────────────────────────────────                           │   │
│  │ • Implement ContextManager skeleton                             │   │
│  │ • Basic KV cache storage (GPU only)                             │   │
│  │ • Ingest + Query endpoints                                      │   │
│  │ • Integration tests                                             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│                                    ▼                                    │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PHASE 2: MEMORY TIERING                                         │   │
│  │ ──────────────────────                                          │   │
│  │ • GPU → CPU → SSD tiering                                       │   │
│  │ • LRU eviction policies                                         │   │
│  │ • Memory pressure handling                                      │   │
│  │ • Cache stats API                                               │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│                                    ▼                                    │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PHASE 3: ADVANCED FEATURES                                      │   │
│  │ ─────────────────────────                                       │   │
│  │ • Hierarchical context partitioning                             │   │
│  │ • Multi-context queries (RAG)                                   │   │
│  │ • Lock-free ingestion pipeline                                  │   │
│  │ • Performance optimizations                                     │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                    │                                    │
│                                    ▼                                    │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PHASE 4: INTEGRATION                                            │   │
│  │ ───────────────────                                             │   │
│  │ • vPID Controller unification                                   │   │
│  │ • Cross-resource transactions                                   │   │
│  │ • OpenAI/Anthropic API extensions                               │   │
│  │ • Desktop app integration                                       │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

### Phase 0: Foundation

**Objective:** Prepare codebase for L2 extension without breaking L1

#### 0.1 Interface Definitions

```cpp
// include/snapllm/interfaces/i_resource_manager.hpp

#pragma once
#include <string>
#include <optional>
#include <future>

namespace snapllm {

// Base interface for all resource managers (Model, Context, etc.)
template<typename ResourceT, typename HandleT>
class IResourceManager {
public:
    virtual ~IResourceManager() = default;

    // Lifecycle
    virtual std::future<HandleT> load_async(const ResourceT& resource) = 0;
    virtual bool unload(const HandleT& handle) = 0;
    virtual bool is_loaded(const HandleT& handle) const = 0;

    // Access
    virtual std::optional<ResourceT> get(const HandleT& handle) const = 0;
    virtual std::vector<HandleT> list() const = 0;

    // Stats
    virtual size_t memory_usage() const = 0;
    virtual size_t count() const = 0;
};

// Specific type aliases
using ModelHandle = std::string;  // vPID
using ContextHandle = std::string;  // context_id

}  // namespace snapllm
```

```cpp
// include/snapllm/interfaces/i_memory_allocator.hpp

#pragma once
#include <cstddef>
#include <optional>

namespace snapllm {

enum class MemoryTier {
    GPU_HBM,    // Fastest, limited
    CPU_RAM,    // Fast, larger
    SSD_NVME,   // Slow, persistent
};

struct MemoryBlock {
    void* ptr;
    size_t size;
    MemoryTier tier;
    std::string owner_id;
};

class IMemoryAllocator {
public:
    virtual ~IMemoryAllocator() = default;

    // Allocation
    virtual std::optional<MemoryBlock> allocate(
        size_t size,
        MemoryTier preferred_tier,
        const std::string& owner_id
    ) = 0;

    virtual void deallocate(const MemoryBlock& block) = 0;

    // Tiering
    virtual bool promote(const std::string& owner_id, MemoryTier target) = 0;
    virtual bool demote(const std::string& owner_id, MemoryTier target) = 0;

    // Stats
    virtual size_t available(MemoryTier tier) const = 0;
    virtual size_t used(MemoryTier tier) const = 0;
};

}  // namespace snapllm
```

#### 0.2 Refactor ModelManager to Use Interfaces

```cpp
// Before (tightly coupled)
class ModelManager {
    std::unordered_map<std::string, llama_model*> models_;
    // Direct llama.cpp calls everywhere
};

// After (interface-based)
class ModelManager : public IResourceManager<ModelSpec, ModelHandle> {
public:
    ModelManager(IMemoryAllocator* allocator, IInferenceEngine* engine);

    std::future<ModelHandle> load_async(const ModelSpec& spec) override;
    bool unload(const ModelHandle& handle) override;
    // ... implements interface

private:
    IMemoryAllocator* allocator_;  // Injected dependency
    IInferenceEngine* engine_;     // Injected dependency
    std::unordered_map<ModelHandle, ModelEntry> models_;
};
```

#### 0.3 Create Memory Management Layer

```cpp
// src/memory/unified_memory_manager.cpp

class UnifiedMemoryManager : public IMemoryAllocator {
public:
    UnifiedMemoryManager(const MemoryConfig& config);

    std::optional<MemoryBlock> allocate(
        size_t size,
        MemoryTier preferred_tier,
        const std::string& owner_id
    ) override {
        // Try preferred tier first
        if (auto block = try_allocate(size, preferred_tier, owner_id)) {
            return block;
        }

        // Fallback to lower tiers
        for (auto tier : fallback_order(preferred_tier)) {
            if (auto block = try_allocate(size, tier, owner_id)) {
                return block;
            }
        }

        // Trigger eviction and retry
        if (evict_for(size, preferred_tier)) {
            return try_allocate(size, preferred_tier, owner_id);
        }

        return std::nullopt;
    }

private:
    GPUAllocator gpu_allocator_;
    CPUAllocator cpu_allocator_;
    SSDAllocator ssd_allocator_;

    LRUEvictionPolicy eviction_policy_;
    std::unordered_map<std::string, MemoryBlock> allocations_;
};
```

#### 0.4 Testing Infrastructure

```cpp
// tests/mocks/mock_memory_allocator.hpp

class MockMemoryAllocator : public IMemoryAllocator {
public:
    MOCK_METHOD(std::optional<MemoryBlock>, allocate,
                (size_t, MemoryTier, const std::string&), (override));
    MOCK_METHOD(void, deallocate, (const MemoryBlock&), (override));
    // ... etc
};

// tests/integration/model_manager_test.cpp

TEST(ModelManagerTest, LoadModelUsesAllocator) {
    MockMemoryAllocator allocator;
    MockInferenceEngine engine;

    EXPECT_CALL(allocator, allocate(_, MemoryTier::GPU_HBM, _))
        .WillOnce(Return(MemoryBlock{...}));

    ModelManager manager(&allocator, &engine);
    auto handle = manager.load_async(ModelSpec{"test"}).get();

    EXPECT_TRUE(manager.is_loaded(handle));
}
```

---

### Phase 1: Core Cache

**Objective:** Minimal viable context caching (GPU only)

#### 1.1 ContextManager Implementation

```cpp
// include/snapllm/context/context_manager.hpp

#pragma once
#include "interfaces/i_resource_manager.hpp"
#include "context/kv_cache.hpp"

namespace snapllm {

struct ContextSpec {
    std::string content;
    ModelHandle model_id;
    ContextConfig config;
};

struct ContextEntry {
    ContextHandle handle;
    KVCache kv_cache;
    ContextMetadata metadata;
    std::chrono::steady_clock::time_point last_access;
    size_t access_count = 0;
};

class ContextManager : public IResourceManager<ContextSpec, ContextHandle> {
public:
    ContextManager(
        IMemoryAllocator* allocator,
        IInferenceEngine* engine,
        ModelManager* model_manager
    );

    // IResourceManager implementation
    std::future<ContextHandle> load_async(const ContextSpec& spec) override;
    bool unload(const ContextHandle& handle) override;
    bool is_loaded(const ContextHandle& handle) const override;
    std::optional<ContextSpec> get(const ContextHandle& handle) const override;
    std::vector<ContextHandle> list() const override;
    size_t memory_usage() const override;
    size_t count() const override;

    // Context-specific operations
    std::optional<KVCacheView> get_kv_cache(const ContextHandle& handle);
    ContextStatus get_status(const ContextHandle& handle) const;

private:
    ContextHandle generate_handle();
    KVCache compute_kv_cache(const std::string& content, const ModelHandle& model_id);

    IMemoryAllocator* allocator_;
    IInferenceEngine* engine_;
    ModelManager* model_manager_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<ContextHandle, ContextEntry> contexts_;
    std::atomic<uint64_t> handle_counter_{0};
};

}  // namespace snapllm
```

```cpp
// src/context/context_manager.cpp

namespace snapllm {

std::future<ContextHandle> ContextManager::load_async(const ContextSpec& spec) {
    return std::async(std::launch::async, [this, spec]() {
        // 1. Validate model exists
        if (!model_manager_->is_loaded(spec.model_id)) {
            throw ContextError("Model not loaded: " + spec.model_id);
        }

        // 2. Generate handle
        auto handle = generate_handle();

        // 3. Tokenize content
        auto tokens = engine_->tokenize(spec.content, spec.model_id);

        // 4. Allocate memory for KV cache
        size_t kv_size = estimate_kv_size(tokens.size(), spec.model_id);
        auto memory = allocator_->allocate(kv_size, MemoryTier::GPU_HBM, handle);
        if (!memory) {
            throw ContextError("Failed to allocate memory for KV cache");
        }

        // 5. Compute KV cache (expensive O(n²) operation)
        auto kv_cache = compute_kv_cache(spec.content, spec.model_id);

        // 6. Store in registry
        {
            std::unique_lock lock(mutex_);
            contexts_[handle] = ContextEntry{
                .handle = handle,
                .kv_cache = std::move(kv_cache),
                .metadata = {
                    .token_count = tokens.size(),
                    .model_id = spec.model_id,
                    .created_at = std::chrono::system_clock::now()
                },
                .last_access = std::chrono::steady_clock::now(),
                .access_count = 0
            };
        }

        return handle;
    });
}

std::optional<KVCacheView> ContextManager::get_kv_cache(const ContextHandle& handle) {
    std::shared_lock lock(mutex_);

    auto it = contexts_.find(handle);
    if (it == contexts_.end()) {
        return std::nullopt;
    }

    // Update access stats (for LRU)
    it->second.last_access = std::chrono::steady_clock::now();
    it->second.access_count++;

    // Return view (non-owning reference)
    return KVCacheView{
        .layers = it->second.kv_cache.layers,
        .sequence_length = it->second.kv_cache.sequence_length
    };
}

}  // namespace snapllm
```

#### 1.2 KV Cache Computation

```cpp
// src/context/kv_compute.cpp

namespace snapllm {

KVCache ContextManager::compute_kv_cache(
    const std::string& content,
    const ModelHandle& model_id
) {
    // Get model from model manager
    auto model = model_manager_->get_model_ptr(model_id);

    // Tokenize
    auto tokens = engine_->tokenize(content, model_id);

    // Get model config
    auto config = model->config();
    size_t num_layers = config.num_layers;
    size_t num_heads = config.num_heads;
    size_t head_dim = config.head_dim;
    size_t seq_len = tokens.size();

    // Allocate KV cache structure
    KVCache kv_cache;
    kv_cache.sequence_length = seq_len;
    kv_cache.num_layers = num_layers;
    kv_cache.num_heads = num_heads;
    kv_cache.head_dim = head_dim;
    kv_cache.layers.resize(num_layers);

    // Process through model to compute KV
    // This is the O(n²) computation we want to do once
    Tensor hidden_states = model->embed(tokens);

    for (size_t layer = 0; layer < num_layers; ++layer) {
        // Compute Q, K, V for all tokens
        auto [q, k, v] = model->compute_qkv(hidden_states, layer);

        // Store K and V in cache
        kv_cache.layers[layer].keys = k.clone();    // [num_heads, seq_len, head_dim]
        kv_cache.layers[layer].values = v.clone();  // [num_heads, seq_len, head_dim]

        // Continue forward pass (needed for next layer's hidden states)
        auto attn_output = model->attention_forward(q, k, v, layer);
        hidden_states = model->ffn_forward(attn_output, layer);
    }

    return kv_cache;
}

}  // namespace snapllm
```

#### 1.3 Query with Cached Context

```cpp
// src/context/cached_query.cpp

namespace snapllm {

GenerationResult QueryProcessor::query_with_context(
    const ContextHandle& ctx_id,
    const std::string& query,
    const GenerationConfig& config
) {
    // 1. Get cached KV (O(1) lookup)
    auto cached_kv = context_manager_->get_kv_cache(ctx_id);
    if (!cached_kv) {
        throw QueryError("Context not found: " + ctx_id);
    }

    // 2. Get model for this context
    auto ctx_meta = context_manager_->get_metadata(ctx_id);
    auto model = model_manager_->get_model_ptr(ctx_meta.model_id);

    // 3. Tokenize query
    auto query_tokens = engine_->tokenize(query, ctx_meta.model_id);

    // 4. Generate with cached KV
    std::vector<int> output_tokens;
    KVCache generation_kv;  // For generated tokens

    Tensor hidden_states = model->embed(query_tokens);

    for (int step = 0; step < config.max_tokens; ++step) {
        // Process through layers
        for (size_t layer = 0; layer < model->num_layers(); ++layer) {
            auto [q, k, v] = model->compute_qkv(hidden_states, layer);

            // Concatenate: [cached_context_kv | generation_kv | current_kv]
            Tensor full_k = concat_kv(
                cached_kv->layers[layer].keys,
                generation_kv.layers[layer].keys,
                k
            );
            Tensor full_v = concat_kv(
                cached_kv->layers[layer].values,
                generation_kv.layers[layer].values,
                v
            );

            // Attention: query attends to full sequence
            auto attn_out = model->attention_forward(q, full_k, full_v, layer);

            // Update generation KV cache
            generation_kv.layers[layer].keys = concat(
                generation_kv.layers[layer].keys, k
            );
            generation_kv.layers[layer].values = concat(
                generation_kv.layers[layer].values, v
            );

            // FFN
            hidden_states = model->ffn_forward(attn_out, layer);
        }

        // Sample next token
        int next_token = sample(hidden_states, config);
        output_tokens.push_back(next_token);

        if (next_token == model->eos_token()) break;

        // Embed for next iteration
        hidden_states = model->embed({next_token});
    }

    return GenerationResult{
        .text = engine_->detokenize(output_tokens, ctx_meta.model_id),
        .tokens = output_tokens,
        .usage = {
            .context_tokens = cached_kv->sequence_length,
            .query_tokens = query_tokens.size(),
            .generated_tokens = output_tokens.size()
        }
    };
}

}  // namespace snapllm
```

#### 1.4 API Endpoints (Phase 1)

```cpp
// src/handlers/context_handler.cpp

namespace snapllm {

void ContextHandler::register_routes(httplib::Server& server) {
    // POST /api/v1/context/ingest
    server.Post("/api/v1/context/ingest", [this](const auto& req, auto& res) {
        try {
            auto body = json::parse(req.body);

            ContextSpec spec{
                .content = body["content"].get<std::string>(),
                .model_id = body["model_id"].get<std::string>(),
                .config = parse_config(body.value("config", json::object()))
            };

            // Start async ingestion
            auto future = context_manager_->load_async(spec);

            // For Phase 1: Wait for completion (blocking)
            // Phase 2: Return immediately with status
            auto handle = future.get();

            res.status = 201;
            res.set_content(json{
                {"context_id", handle},
                {"status", "ready"},
                {"token_count", context_manager_->get_metadata(handle).token_count}
            }.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/v1/context/{id}/query
    server.Post(R"(/api/v1/context/([^/]+)/query)", [this](const auto& req, auto& res) {
        try {
            auto ctx_id = req.matches[1].str();
            auto body = json::parse(req.body);

            auto result = query_processor_->query_with_context(
                ctx_id,
                body["query"].get<std::string>(),
                parse_generation_config(body.value("config", json::object()))
            );

            res.status = 200;
            res.set_content(json{
                {"text", result.text},
                {"usage", {
                    {"context_tokens", result.usage.context_tokens},
                    {"query_tokens", result.usage.query_tokens},
                    {"generated_tokens", result.usage.generated_tokens}
                }}
            }.dump(), "application/json");

        } catch (const ContextNotFoundError& e) {
            res.status = 404;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // GET /api/v1/context/{id}/status
    server.Get(R"(/api/v1/context/([^/]+)/status)", [this](const auto& req, auto& res) {
        auto ctx_id = req.matches[1].str();
        auto status = context_manager_->get_status(ctx_id);

        if (!status) {
            res.status = 404;
            res.set_content(json{{"error", "Context not found"}}.dump(), "application/json");
            return;
        }

        res.status = 200;
        res.set_content(json{
            {"context_id", ctx_id},
            {"status", status->state},
            {"token_count", status->token_count},
            {"memory_mb", status->memory_bytes / (1024 * 1024)},
            {"access_count", status->access_count}
        }.dump(), "application/json");
    });

    // DELETE /api/v1/context/{id}
    server.Delete(R"(/api/v1/context/([^/]+))", [this](const auto& req, auto& res) {
        auto ctx_id = req.matches[1].str();

        if (context_manager_->unload(ctx_id)) {
            res.status = 204;
        } else {
            res.status = 404;
            res.set_content(json{{"error", "Context not found"}}.dump(), "application/json");
        }
    });
}

}  // namespace snapllm
```

---

### Phase 2: Memory Tiering

**Objective:** Implement GPU → CPU → SSD tiering with automatic management

#### 2.1 Tiered Memory Manager

```cpp
// src/memory/tiered_memory_manager.cpp

namespace snapllm {

class TieredMemoryManager : public IMemoryAllocator {
public:
    TieredMemoryManager(const TieringConfig& config)
        : config_(config)
        , gpu_pool_(config.gpu_capacity)
        , cpu_pool_(config.cpu_capacity)
        , ssd_store_(config.ssd_path, config.ssd_capacity)
    {
        // Start background tiering thread
        tiering_thread_ = std::thread([this] { tiering_loop(); });
    }

    std::optional<MemoryBlock> allocate(
        size_t size,
        MemoryTier preferred_tier,
        const std::string& owner_id
    ) override {
        std::lock_guard lock(mutex_);

        // Try preferred tier
        if (auto block = try_tier(size, preferred_tier, owner_id)) {
            record_allocation(owner_id, preferred_tier, size);
            return block;
        }

        // Try lower tiers
        for (auto tier : get_fallback_order(preferred_tier)) {
            if (auto block = try_tier(size, tier, owner_id)) {
                record_allocation(owner_id, tier, size);
                return block;
            }
        }

        // Need eviction
        if (try_evict(size, preferred_tier)) {
            if (auto block = try_tier(size, preferred_tier, owner_id)) {
                record_allocation(owner_id, preferred_tier, size);
                return block;
            }
        }

        return std::nullopt;
    }

    bool promote(const std::string& owner_id, MemoryTier target) override {
        std::lock_guard lock(mutex_);

        auto it = allocations_.find(owner_id);
        if (it == allocations_.end()) return false;

        auto current_tier = it->second.tier;
        if (static_cast<int>(target) >= static_cast<int>(current_tier)) {
            return false;  // Can't promote to same or lower tier
        }

        // Allocate in target tier
        auto new_block = try_tier(it->second.size, target, owner_id);
        if (!new_block) return false;

        // Copy data
        copy_data(it->second, *new_block);

        // Deallocate from old tier
        deallocate_from_tier(it->second);

        // Update record
        it->second = *new_block;

        return true;
    }

    bool demote(const std::string& owner_id, MemoryTier target) override {
        std::lock_guard lock(mutex_);

        auto it = allocations_.find(owner_id);
        if (it == allocations_.end()) return false;

        auto current_tier = it->second.tier;
        if (static_cast<int>(target) <= static_cast<int>(current_tier)) {
            return false;  // Can't demote to same or higher tier
        }

        // Allocate in target tier
        auto new_block = try_tier(it->second.size, target, owner_id);
        if (!new_block) return false;

        // Copy data
        copy_data(it->second, *new_block);

        // Deallocate from old tier
        deallocate_from_tier(it->second);

        // Update record
        it->second = *new_block;

        return true;
    }

private:
    void tiering_loop() {
        while (running_) {
            std::this_thread::sleep_for(config_.tiering_interval);

            std::lock_guard lock(mutex_);

            // Promote hot items
            for (auto& [id, stats] : access_stats_) {
                if (stats.recent_accesses > config_.hot_threshold) {
                    auto it = allocations_.find(id);
                    if (it != allocations_.end() && it->second.tier != MemoryTier::GPU_HBM) {
                        promote(id, higher_tier(it->second.tier));
                    }
                }
            }

            // Demote cold items
            auto now = std::chrono::steady_clock::now();
            for (auto& [id, stats] : access_stats_) {
                auto idle_time = now - stats.last_access;

                if (idle_time > config_.cold_timeout) {
                    auto it = allocations_.find(id);
                    if (it != allocations_.end() && it->second.tier != MemoryTier::SSD_NVME) {
                        demote(id, lower_tier(it->second.tier));
                    }
                }
            }

            // Reset access counters
            for (auto& [id, stats] : access_stats_) {
                stats.recent_accesses = 0;
            }
        }
    }

    bool try_evict(size_t needed, MemoryTier tier) {
        // Get candidates sorted by LRU
        auto candidates = get_eviction_candidates(tier);

        size_t freed = 0;
        for (const auto& candidate : candidates) {
            // Demote to lower tier instead of full eviction
            auto lower = lower_tier(tier);
            if (lower != tier && demote(candidate, lower)) {
                freed += allocations_[candidate].size;
                if (freed >= needed) return true;
            }
        }

        // If still not enough, actually evict from lowest tier
        if (tier == MemoryTier::SSD_NVME) {
            for (const auto& candidate : candidates) {
                deallocate(allocations_[candidate]);
                allocations_.erase(candidate);
                freed += allocations_[candidate].size;
                if (freed >= needed) return true;
            }
        }

        return freed >= needed;
    }

    TieringConfig config_;
    GPUMemoryPool gpu_pool_;
    CPUMemoryPool cpu_pool_;
    SSDStore ssd_store_;

    std::mutex mutex_;
    std::unordered_map<std::string, MemoryBlock> allocations_;
    std::unordered_map<std::string, AccessStats> access_stats_;

    std::thread tiering_thread_;
    std::atomic<bool> running_{true};
};

}  // namespace snapllm
```

#### 2.2 Cache Statistics API

```cpp
// src/handlers/cache_stats_handler.cpp

server.Get("/api/v1/cache/stats", [this](const auto& req, auto& res) {
    auto stats = memory_manager_->get_stats();

    res.status = 200;
    res.set_content(json{
        {"gpu_cache", {
            {"capacity_mb", stats.gpu.capacity / MB},
            {"used_mb", stats.gpu.used / MB},
            {"contexts_count", stats.gpu.items},
            {"hit_rate", stats.gpu.hit_rate}
        }},
        {"cpu_cache", {
            {"capacity_mb", stats.cpu.capacity / MB},
            {"used_mb", stats.cpu.used / MB},
            {"contexts_count", stats.cpu.items},
            {"hit_rate", stats.cpu.hit_rate}
        }},
        {"ssd_cache", {
            {"capacity_mb", stats.ssd.capacity / MB},
            {"used_mb", stats.ssd.used / MB},
            {"contexts_count", stats.ssd.items},
            {"hit_rate", stats.ssd.hit_rate}
        }},
        {"tiering", {
            {"promotions_total", stats.promotions},
            {"demotions_total", stats.demotions},
            {"evictions_total", stats.evictions}
        }},
        {"performance", {
            {"avg_query_latency_ms", stats.avg_query_latency_ms},
            {"cache_hit_latency_ms", stats.cache_hit_latency_ms},
            {"cache_miss_latency_ms", stats.cache_miss_latency_ms}
        }}
    }.dump(), "application/json");
});
```

---

### Phase 3: Advanced Features

**Objective:** Hierarchical partitioning, multi-context, lock-free pipelines

#### 3.1 Hierarchical Context Partitioning

```cpp
// src/context/hierarchical_partitioner.cpp

namespace snapllm {

struct ContextChunk {
    std::string chunk_id;
    size_t start_offset;
    size_t end_offset;
    int level;  // 0 = leaf, higher = summaries
    std::vector<std::string> child_ids;
    KVCache kv_cache;
};

class HierarchicalPartitioner {
public:
    std::vector<ContextChunk> partition(
        const std::string& content,
        const PartitionConfig& config
    ) {
        std::vector<ContextChunk> chunks;

        // Level 0: Leaf chunks
        auto tokens = tokenizer_->encode(content);
        size_t num_chunks = (tokens.size() + config.chunk_size - 1) / config.chunk_size;

        for (size_t i = 0; i < num_chunks; ++i) {
            size_t start = i * (config.chunk_size - config.overlap);
            size_t end = std::min(start + config.chunk_size, tokens.size());

            ContextChunk chunk;
            chunk.chunk_id = generate_chunk_id();
            chunk.start_offset = start;
            chunk.end_offset = end;
            chunk.level = 0;

            // Compute KV for this chunk
            auto chunk_tokens = slice(tokens, start, end);
            chunk.kv_cache = compute_kv(chunk_tokens);

            chunks.push_back(std::move(chunk));
        }

        // Build hierarchy (levels 1+)
        build_hierarchy(chunks, config.max_levels);

        return chunks;
    }

private:
    void build_hierarchy(std::vector<ContextChunk>& chunks, int max_levels) {
        for (int level = 1; level < max_levels; ++level) {
            auto prev_level = get_chunks_at_level(chunks, level - 1);

            if (prev_level.size() <= 1) break;  // No need for more levels

            // Group chunks and create summaries
            for (size_t i = 0; i < prev_level.size(); i += group_size_) {
                auto group = slice(prev_level, i, std::min(i + group_size_, prev_level.size()));

                ContextChunk summary;
                summary.chunk_id = generate_chunk_id();
                summary.level = level;

                // Store child references
                for (const auto& child : group) {
                    summary.child_ids.push_back(child->chunk_id);
                }

                // Generate summary text and compute its KV
                auto summary_text = generate_summary(group);
                auto summary_tokens = tokenizer_->encode(summary_text);
                summary.kv_cache = compute_kv(summary_tokens);

                chunks.push_back(std::move(summary));
            }
        }
    }
};

}  // namespace snapllm
```

#### 3.2 Multi-Context Query (RAG)

```cpp
// src/context/multi_context_query.cpp

namespace snapllm {

GenerationResult QueryProcessor::query_multi_context(
    const std::vector<ContextHandle>& ctx_ids,
    const std::string& query,
    const MultiContextConfig& config
) {
    // 1. Gather all KV caches
    std::vector<KVCacheView> kv_caches;
    size_t total_context_tokens = 0;

    for (const auto& ctx_id : ctx_ids) {
        auto kv = context_manager_->get_kv_cache(ctx_id);
        if (!kv) {
            throw QueryError("Context not found: " + ctx_id);
        }
        kv_caches.push_back(*kv);
        total_context_tokens += kv->sequence_length;
    }

    // 2. Merge strategy
    KVCacheView merged_kv;
    switch (config.merge_strategy) {
        case MergeStrategy::CONCATENATE:
            merged_kv = concatenate_kv_caches(kv_caches);
            break;

        case MergeStrategy::INTERLEAVE:
            merged_kv = interleave_kv_caches(kv_caches);
            break;

        case MergeStrategy::WEIGHTED:
            merged_kv = weighted_merge_kv_caches(kv_caches, config.weights);
            break;
    }

    // 3. Query with merged context
    auto result = query_with_kv_cache(merged_kv, query, config.generation);

    result.usage.context_tokens = total_context_tokens;
    result.context_ids = ctx_ids;

    return result;
}

}  // namespace snapllm
```

#### 3.3 Lock-Free Ingestion Pipeline

```cpp
// src/context/lockfree_ingestion.cpp

namespace snapllm {

class LockFreeIngestionPipeline {
public:
    LockFreeIngestionPipeline(
        size_t num_workers,
        ContextManager* context_manager
    ) : context_manager_(context_manager) {
        // Start worker threads
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~LockFreeIngestionPipeline() {
        shutdown();
    }

    std::future<ContextHandle> submit(IngestionTask task) {
        auto promise = std::make_shared<std::promise<ContextHandle>>();
        auto future = promise->get_future();

        task.promise = promise;
        task.submitted_at = std::chrono::steady_clock::now();

        // Lock-free enqueue
        while (!task_queue_.try_enqueue(std::move(task))) {
            std::this_thread::yield();
        }

        pending_count_.fetch_add(1, std::memory_order_relaxed);

        return future;
    }

    PipelineStats get_stats() const {
        return PipelineStats{
            .pending = pending_count_.load(std::memory_order_relaxed),
            .processing = processing_count_.load(std::memory_order_relaxed),
            .completed = completed_count_.load(std::memory_order_relaxed),
            .failed = failed_count_.load(std::memory_order_relaxed),
            .avg_latency_ms = avg_latency_ms_.load(std::memory_order_relaxed)
        };
    }

private:
    void worker_loop(size_t worker_id) {
        while (running_.load(std::memory_order_acquire)) {
            IngestionTask task;

            if (task_queue_.try_dequeue(task)) {
                pending_count_.fetch_sub(1, std::memory_order_relaxed);
                processing_count_.fetch_add(1, std::memory_order_relaxed);

                process_task(task, worker_id);

                processing_count_.fetch_sub(1, std::memory_order_relaxed);
            } else {
                // No work, back off
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }

    void process_task(IngestionTask& task, size_t worker_id) {
        try {
            // Compute KV cache (CPU-bound, O(n²))
            auto handle = context_manager_->load_sync(task.spec);

            // Record latency
            auto latency = std::chrono::steady_clock::now() - task.submitted_at;
            auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(latency).count();
            update_avg_latency(latency_ms);

            // Fulfill promise
            task.promise->set_value(handle);
            completed_count_.fetch_add(1, std::memory_order_relaxed);

        } catch (const std::exception& e) {
            task.promise->set_exception(std::current_exception());
            failed_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void update_avg_latency(int64_t new_latency) {
        // Exponential moving average
        double alpha = 0.1;
        double current = avg_latency_ms_.load(std::memory_order_relaxed);
        double updated = (1 - alpha) * current + alpha * new_latency;
        avg_latency_ms_.store(updated, std::memory_order_relaxed);
    }

    void shutdown() {
        running_.store(false, std::memory_order_release);
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    ContextManager* context_manager_;
    moodycamel::ConcurrentQueue<IngestionTask> task_queue_;
    std::vector<std::thread> workers_;

    std::atomic<bool> running_{true};
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> processing_count_{0};
    std::atomic<size_t> completed_count_{0};
    std::atomic<size_t> failed_count_{0};
    std::atomic<double> avg_latency_ms_{0};
};

}  // namespace snapllm
```

---

### Phase 4: Integration

**Objective:** Unify vPID levels, integrate with existing APIs

#### 4.1 vPID Controller

```cpp
// src/vpid/vpid_controller.cpp

namespace snapllm {

class VPIDController {
public:
    VPIDController(
        ModelManager* model_manager,
        ContextManager* context_manager,
        UnifiedMemoryManager* memory_manager
    ) : model_manager_(model_manager)
      , context_manager_(context_manager)
      , memory_manager_(memory_manager)
    {}

    // Unified resource operations
    template<typename... Ops>
    std::future<TransactionResult> execute_transaction(Ops&&... ops) {
        return std::async(std::launch::async, [this, ops = std::make_tuple(std::forward<Ops>(ops)...)]() {
            TransactionResult result;

            try {
                // Execute operations in order
                std::apply([this, &result](auto&&... op) {
                    (execute_op(op, result), ...);
                }, ops);

                result.success = true;
            } catch (const std::exception& e) {
                result.success = false;
                result.error = e.what();
                // Rollback any completed operations
                rollback(result.completed_ops);
            }

            return result;
        });
    }

    // High-level operations
    GenerationResult generate(const GenerationRequest& req) {
        // Auto-select based on request
        if (req.context_id) {
            // Use cached context (L2)
            return context_manager_->query(*req.context_id, req.prompt, req.config);
        } else {
            // Direct generation (L1)
            return model_manager_->generate(req.model_id, req.prompt, req.config);
        }
    }

    // Stats across all levels
    VPIDStats get_stats() const {
        return VPIDStats{
            .l1 = {
                .loaded_models = model_manager_->count(),
                .total_model_memory = model_manager_->memory_usage()
            },
            .l2 = {
                .cached_contexts = context_manager_->count(),
                .total_context_memory = context_manager_->memory_usage(),
                .cache_hit_rate = context_manager_->hit_rate()
            },
            .memory = memory_manager_->get_stats()
        };
    }

private:
    void execute_op(const SwitchModel& op, TransactionResult& result) {
        model_manager_->switch_to(op.model_id);
        result.completed_ops.push_back({"switch_model", op.model_id});
    }

    void execute_op(const LoadContext& op, TransactionResult& result) {
        context_manager_->ensure_loaded(op.context_id);
        result.completed_ops.push_back({"load_context", op.context_id});
    }

    void execute_op(const WarmCache& op, TransactionResult& result) {
        memory_manager_->promote(op.context_id, MemoryTier::GPU_HBM);
        result.completed_ops.push_back({"warm_cache", op.context_id});
    }

    ModelManager* model_manager_;
    ContextManager* context_manager_;
    UnifiedMemoryManager* memory_manager_;
};

}  // namespace snapllm
```

#### 4.2 Extended OpenAI API

```cpp
// src/handlers/openai_handler.cpp (extended)

// Extend /v1/chat/completions to support context_id
server.Post("/v1/chat/completions", [this](const auto& req, auto& res) {
    auto body = json::parse(req.body);

    // Check for context_id (L2 extension)
    std::optional<std::string> context_id;
    if (body.contains("context_id")) {
        context_id = body["context_id"].get<std::string>();
    }

    GenerationRequest gen_req{
        .model_id = body["model"].get<std::string>(),
        .messages = parse_messages(body["messages"]),
        .context_id = context_id,
        .config = parse_config(body)
    };

    auto result = vpid_controller_->generate(gen_req);

    // Format as OpenAI response
    json response = format_openai_response(result);

    // Add L2 info if context was used
    if (context_id) {
        response["usage"]["context_cache_hit"] = true;
        response["usage"]["cached_context_tokens"] = result.usage.context_tokens;
    }

    res.status = 200;
    res.set_content(response.dump(), "application/json");
});
```

#### 4.3 Extended Anthropic API

```cpp
// src/handlers/anthropic_handler.cpp (extended)

server.Post("/v1/messages", [this](const auto& req, auto& res) {
    auto body = json::parse(req.body);

    // Check for context_id (L2 extension)
    std::optional<std::string> context_id;
    if (body.contains("context_id")) {
        context_id = body["context_id"].get<std::string>();
    }

    // ... rest of Anthropic message handling

    // Add L2 usage info
    if (context_id) {
        response["usage"]["context_cache_hit"] = true;
        response["usage"]["cached_context_tokens"] = result.usage.context_tokens;
    }
});
```

---

## Module Architecture & Boundaries

### Directory Structure

```
src/
├── server/                    # Layer 6: Presentation
│   ├── http_server.cpp
│   ├── websocket_server.cpp
│   └── CMakeLists.txt
│
├── handlers/                  # Layer 5: API
│   ├── openai_handler.cpp
│   ├── anthropic_handler.cpp
│   ├── context_handler.cpp    # [NEW]
│   ├── cache_handler.cpp      # [NEW]
│   └── CMakeLists.txt
│
├── vpid/                      # Layer 4: Orchestration [NEW]
│   ├── vpid_controller.cpp
│   ├── transaction.cpp
│   └── CMakeLists.txt
│
├── model/                     # Layer 3A: Model Manager
│   ├── model_manager.cpp
│   ├── model_registry.cpp
│   └── CMakeLists.txt
│
├── context/                   # Layer 3B: Context Manager [NEW]
│   ├── context_manager.cpp
│   ├── kv_compute.cpp
│   ├── hierarchical_partitioner.cpp
│   ├── query_processor.cpp
│   ├── lockfree_pipeline.cpp
│   └── CMakeLists.txt
│
├── memory/                    # Layer 2: Memory Management [NEW]
│   ├── unified_memory_manager.cpp
│   ├── tiered_allocator.cpp
│   ├── eviction_policy.cpp
│   ├── gpu_pool.cpp
│   ├── cpu_pool.cpp
│   ├── ssd_store.cpp
│   └── CMakeLists.txt
│
├── inference/                 # Layer 1: Inference Engine
│   ├── llama_engine.cpp
│   ├── kv_cache.cpp
│   └── CMakeLists.txt
│
├── backend/                   # Layer 0: Hardware Abstraction
│   ├── cuda_backend.cpp
│   ├── cpu_backend.cpp
│   └── CMakeLists.txt
│
└── interfaces/                # Shared interfaces
    ├── i_resource_manager.hpp
    ├── i_memory_allocator.hpp
    ├── i_inference_engine.hpp
    └── CMakeLists.txt
```

### Dependency Graph

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    MODULE DEPENDENCY GRAPH                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  server ──────────────────────────────────────────────────────────────┐│
│    │                                                                   ││
│    ▼                                                                   ││
│  handlers ────────────────────────────────────────────────────────────┤│
│    │                                                                   ││
│    ▼                                                                   ││
│  vpid ────────────────────────────────────────────────────────────────┤│
│    │                                                                   ││
│    ├──────────────┬──────────────┐                                    ││
│    ▼              ▼              │                                    ││
│  model         context           │                                    ││
│    │              │              │                                    ││
│    └──────────────┴──────────────┘                                    ││
│                   │                                                   ││
│                   ▼                                                   ││
│              memory ──────────────────────────────────────────────────┤│
│                   │                                                   ││
│                   ▼                                                   ││
│              inference ───────────────────────────────────────────────┤│
│                   │                                                   ││
│                   ▼                                                   ││
│              backend ─────────────────────────────────────────────────┤│
│                   │                                                   ││
│                   ▼                                                   ││
│              interfaces ◄─────────────────────────────────────────────┘│
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

Legend:
  ───▶  Depends on (compiles against)
  ◄───  Implements interface from
```

---

## Interface Contracts

### IResourceManager Contract

```cpp
/**
 * Contract: IResourceManager<R, H>
 *
 * Invariants:
 * 1. load_async() returns a unique handle for each successful load
 * 2. is_loaded(h) == true iff get(h).has_value()
 * 3. unload(h) returns true only if is_loaded(h) was true
 * 4. list() returns exactly the handles for which is_loaded() is true
 * 5. memory_usage() >= 0 and reflects actual memory consumed
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Concurrent load_async() calls may execute in parallel
 * - load_async() and unload() for the same handle are serialized
 *
 * Error Handling:
 * - load_async() future throws on failure (never returns invalid handle)
 * - get() returns nullopt for invalid/unloaded handles (never throws)
 * - unload() returns false for invalid handles (never throws)
 */
```

### IMemoryAllocator Contract

```cpp
/**
 * Contract: IMemoryAllocator
 *
 * Invariants:
 * 1. allocate() returns nullopt if allocation fails (never throws)
 * 2. Returned MemoryBlock.ptr is valid until deallocate() is called
 * 3. available(tier) + used(tier) <= total_capacity(tier)
 * 4. promote/demote preserve data content exactly
 * 5. deallocate() of same block twice is undefined behavior
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Concurrent allocate() calls may race; some may fail even with space
 * - allocate() and deallocate() for same owner_id are serialized
 *
 * Memory Guarantees:
 * - GPU allocations are aligned to 256 bytes
 * - CPU allocations are aligned to 64 bytes
 * - SSD allocations are page-aligned
 */
```

---

## Migration Strategy

### Incremental Rollout

```
┌─────────────────────────────────────────────────────────────────┐
│                    MIGRATION STAGES                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  STAGE 1: Shadow Mode                                           │
│  ────────────────────                                          │
│  • L2 running alongside L1                                     │
│  • Same requests, compare results                              │
│  • No user-facing changes                                      │
│  • Collect performance metrics                                 │
│                                                                 │
│  STAGE 2: Opt-In                                                │
│  ───────────                                                   │
│  • L2 endpoints available but not default                      │
│  • Documentation and migration guides                          │
│  • Early adopter feedback                                      │
│                                                                 │
│  STAGE 3: Gradual Rollout                                       │
│  ────────────────────                                          │
│  • Feature flag: 10% → 50% → 100%                              │
│  • Monitor error rates and latency                             │
│  • Rollback capability                                         │
│                                                                 │
│  STAGE 4: Default                                               │
│  ──────────                                                    │
│  • L2 is default for applicable requests                       │
│  • L1-only mode still available via config                     │
│  • Deprecation notices for L1-only features                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Backwards Compatibility Guarantees

| API | L1 Behavior | L2 Behavior | Compatibility |
|-----|-------------|-------------|---------------|
| /v1/chat/completions | Works | Works + context_id | ✓ Backwards |
| /v1/messages | Works | Works + context_id | ✓ Backwards |
| /api/v1/models/* | Works | Works unchanged | ✓ Identical |
| /api/v1/context/* | 404 | Full functionality | ✓ New (no break) |
| Memory limits | Model-based | Unified pool | ✓ Config migration |

---

## Testing Strategy

### Test Pyramid

```
┌─────────────────────────────────────────────────────────────────┐
│                    TEST PYRAMID                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│                         ▲                                       │
│                        /│\         E2E Tests (5%)               │
│                       / │ \        - Full API workflows         │
│                      /  │  \       - Multi-model + context      │
│                     /   │   \                                   │
│                    ─────┼─────                                  │
│                   /     │     \    Integration Tests (20%)      │
│                  /      │      \   - Cross-module interactions  │
│                 /       │       \  - Memory tiering flows       │
│                /        │        \ - Concurrent operations      │
│               ──────────┼──────────                             │
│              /          │          \                            │
│             /           │           \  Unit Tests (75%)         │
│            /            │            \ - Each module isolated   │
│           /             │             \- Mock dependencies      │
│          /              │              \- Edge cases            │
│         ────────────────┴────────────────                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Test Categories

```cpp
// Unit test example: ContextManager
TEST(ContextManagerTest, LoadAsyncReturnsUniqueHandles) {
    MockMemoryAllocator allocator;
    MockInferenceEngine engine;
    MockModelManager model_manager;

    EXPECT_CALL(model_manager, is_loaded("test_model"))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(allocator, allocate(_, _, _))
        .WillRepeatedly(Return(MemoryBlock{...}));

    ContextManager cm(&allocator, &engine, &model_manager);

    auto handle1 = cm.load_async(ContextSpec{.model_id = "test_model", .content = "A"}).get();
    auto handle2 = cm.load_async(ContextSpec{.model_id = "test_model", .content = "B"}).get();

    EXPECT_NE(handle1, handle2);
}

// Integration test example: Memory tiering
TEST(TieringIntegrationTest, DemotesOnMemoryPressure) {
    TieredMemoryManager mm(TieringConfig{.gpu_capacity = 1000});
    ContextManager cm(&mm, ...);

    // Fill GPU
    auto ctx1 = cm.load_async(large_context).get();
    EXPECT_EQ(mm.tier_of(ctx1), MemoryTier::GPU_HBM);

    // Trigger pressure
    auto ctx2 = cm.load_async(large_context).get();

    // ctx1 should be demoted
    EXPECT_EQ(mm.tier_of(ctx1), MemoryTier::CPU_RAM);
    EXPECT_EQ(mm.tier_of(ctx2), MemoryTier::GPU_HBM);
}

// E2E test example: Full workflow
TEST(E2ETest, IngestQueryDeleteWorkflow) {
    SnapLLMServer server;
    server.start();

    // Ingest
    auto ingest_response = http_post(server.url() + "/api/v1/context/ingest", {
        {"model_id", "test"},
        {"content", "The quick brown fox..."}
    });
    EXPECT_EQ(ingest_response.status, 201);
    auto ctx_id = ingest_response.json()["context_id"];

    // Query
    auto query_response = http_post(server.url() + "/api/v1/context/" + ctx_id + "/query", {
        {"query", "What color is the fox?"}
    });
    EXPECT_EQ(query_response.status, 200);
    EXPECT_THAT(query_response.json()["text"], HasSubstr("brown"));

    // Delete
    auto delete_response = http_delete(server.url() + "/api/v1/context/" + ctx_id);
    EXPECT_EQ(delete_response.status, 204);

    // Verify deleted
    auto status_response = http_get(server.url() + "/api/v1/context/" + ctx_id + "/status");
    EXPECT_EQ(status_response.status, 404);
}
```

### Performance Benchmarks

```cpp
// Benchmark: Query latency with cached context
BENCHMARK(QueryWithCachedContext) {
    // Setup
    auto ctx_id = context_manager.load_async(large_context).get();

    // Warm cache
    context_manager.query(ctx_id, "warmup", {});

    // Benchmark
    for (auto _ : state) {
        auto result = context_manager.query(ctx_id, "What is X?", {});
        benchmark::DoNotOptimize(result);
    }
}

// Expected results:
// - First query (cache miss): ~2500ms for 50K context
// - Subsequent queries (cache hit): ~85ms
```

---

## Risk Analysis & Mitigation

### Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| GPU OOM with many contexts | High | High | Aggressive tiering, memory budgets |
| KV cache corruption | Low | Critical | Checksums, validation on access |
| Race conditions | Medium | High | Lock-free where possible, thorough testing |
| SSD latency spikes | Medium | Medium | Async prefetch, SSD health monitoring |
| API breaking changes | Low | High | Versioning, deprecation policy |

### Mitigation Strategies

```cpp
// Risk: GPU OOM
// Mitigation: Memory budget enforcement

class MemoryBudgetEnforcer {
public:
    bool request_allocation(size_t size, MemoryTier tier) {
        std::lock_guard lock(mutex_);

        if (current_usage_[tier] + size > budget_[tier]) {
            // Trigger eviction
            size_t to_free = (current_usage_[tier] + size) - budget_[tier];
            if (!eviction_policy_->evict(tier, to_free)) {
                return false;  // Cannot allocate
            }
        }

        current_usage_[tier] += size;
        return true;
    }

private:
    std::unordered_map<MemoryTier, size_t> budget_;
    std::unordered_map<MemoryTier, size_t> current_usage_;
    IEvictionPolicy* eviction_policy_;
    std::mutex mutex_;
};
```

```cpp
// Risk: KV cache corruption
// Mitigation: Checksum validation

struct KVCacheWithChecksum {
    KVCache data;
    uint32_t checksum;

    static uint32_t compute_checksum(const KVCache& cache) {
        // CRC32 over key data
        uint32_t crc = 0;
        for (const auto& layer : cache.layers) {
            crc = crc32(crc, layer.keys.data(), layer.keys.size_bytes());
        }
        return crc;
    }

    bool validate() const {
        return checksum == compute_checksum(data);
    }
};
```

---

## Future Architecture Roadmap

### vPID Level 3: Distributed Corpus

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    FUTURE: vPID LEVEL 3                                 │
│                    Distributed Corpus Architecture                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐              │
│   │   Node A    │     │   Node B    │     │   Node C    │              │
│   │  ┌───────┐  │     │  ┌───────┐  │     │  ┌───────┐  │              │
│   │  │ L1-L2 │  │◄───►│  │ L1-L2 │  │◄───►│  │ L1-L2 │  │              │
│   │  └───────┘  │     │  └───────┘  │     │  └───────┘  │              │
│   └──────┬──────┘     └──────┬──────┘     └──────┬──────┘              │
│          │                   │                   │                      │
│          └───────────────────┼───────────────────┘                      │
│                              │                                          │
│                              ▼                                          │
│              ┌───────────────────────────────┐                          │
│              │      L3 Coordinator            │                          │
│              │  - Distributed index           │                          │
│              │  - Query routing               │                          │
│              │  - Cache coherence             │                          │
│              │  - Replication management      │                          │
│              └───────────────────────────────┘                          │
│                              │                                          │
│                              ▼                                          │
│              ┌───────────────────────────────┐                          │
│              │      Shared Storage            │                          │
│              │  - Distributed KV cache        │                          │
│              │  - Corpus index                │                          │
│              │  - Model registry              │                          │
│              └───────────────────────────────┘                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

L3 Features:
  - Distributed context caching across nodes
  - Automatic shard placement and rebalancing
  - Query routing to node with cache hit
  - Cross-node cache coherence protocol
  - Fault tolerance with replication
```

### Evolution Timeline

```
┌─────────────────────────────────────────────────────────────────┐
│                    EVOLUTION TIMELINE                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  2024 Q4: vPID L1 ✓                                            │
│  ─────────────────                                             │
│  • Multi-model management                                      │
│  • <1ms model switching                                        │
│  • OpenAI + Anthropic API                                      │
│                                                                 │
│  2025 Q1-Q2: vPID L2                                           │
│  ──────────────────                                            │
│  • Phase 0: Foundation                                         │
│  • Phase 1: Core cache                                         │
│  • Phase 2: Memory tiering                                     │
│                                                                 │
│  2025 Q3-Q4: vPID L2 Advanced                                  │
│  ────────────────────────                                      │
│  • Phase 3: Hierarchical partitioning                          │
│  • Phase 4: Full integration                                   │
│  • Production hardening                                        │
│                                                                 │
│  2026: vPID L3 (Future)                                        │
│  ─────────────────────                                         │
│  • Distributed architecture                                    │
│  • Multi-node coordination                                     │
│  • Enterprise scale                                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Appendix: Decision Records

### ADR-005: Separate ContextManager from ModelManager

**Status:** Accepted

**Context:**
Could extend ModelManager to handle contexts, or create separate ContextManager.

**Decision:**
Create separate ContextManager with its own lifecycle.

**Rationale:**
1. Single Responsibility: Models and contexts have different lifecycles
2. Independent Evolution: Can improve context caching without touching model code
3. Clear Boundaries: Easier to reason about and test
4. Future Flexibility: L3 may have very different context needs

**Consequences:**
- More code initially
- Need vPID Controller for coordination
- Cleaner architecture long-term

---

### ADR-006: Lock-Free Ingestion Pipeline

**Status:** Accepted

**Context:**
Ingestion is CPU-bound (O(n²)). Could use mutex-based queue or lock-free.

**Decision:**
Use lock-free concurrent queue (moodycamel::ConcurrentQueue).

**Rationale:**
1. Ingestion shouldn't block queries
2. High throughput for batch ingestion
3. Predictable latency without lock contention

**Consequences:**
- More complex implementation
- Need careful memory ordering
- Better performance under load

---

### ADR-007: Three-Tier Memory Hierarchy

**Status:** Accepted

**Context:**
GPU memory is limited. Could use GPU-only, GPU+CPU, or GPU+CPU+SSD.

**Decision:**
Implement GPU → CPU → SSD tiering with automatic management.

**Rationale:**
1. Maximize context cache capacity
2. Hot/warm/cold access patterns natural fit
3. SSD persistence survives restarts
4. Cost-effective for large deployments

**Consequences:**
- More complex memory management
- Need tiering heuristics
- SSD latency for cold contexts
- Better capacity utilization

---

*Document generated for SnapLLM Project - January 2026*
