# SnapLLM Enterprise UI Suite
## Comprehensive Documentation

**Version:** 1.0.0
**Last Updated:** December 2024
**Platform:** Web + Desktop (Tauri)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Technology Stack](#3-technology-stack)
4. [Design System](#4-design-system)
5. [Component Library](#5-component-library)
6. [Pages & Features](#6-pages--features)
7. [API Integration](#7-api-integration)
8. [State Management](#8-state-management)
9. [Routing](#9-routing)
10. [Development Guide](#10-development-guide)
11. [Deployment](#11-deployment)

---

## 1. Overview

### 1.1 Introduction

SnapLLM Enterprise UI Suite is a billion-dollar grade enterprise application for managing multi-model AI inference. It provides a comprehensive interface for:

- **Multi-Model Management**: Load, configure, and switch between multiple AI models with sub-millisecond latency using vPID (Virtual Processing-In-Disk) architecture
- **GenAI Studio**: Chat, Image Generation, Video Generation, and Vision/Multimodal capabilities
- **RAG Integration**: Knowledge base management with document ingestion and retrieval
- **Agentic AI**: Autonomous AI agents with tool use and workflow orchestration
- **Enterprise Features**: Team management, API keys, audit logging, security controls
- **Analytics**: Real-time performance monitoring and observability

### 1.2 Key Differentiators

| Feature | Description |
|---------|-------------|
| **vPID Architecture** | Sub-millisecond model switching via 3-tier cache (HOT/WARM/COLD) |
| **Multi-Modal Support** | Text, Image, Video, Audio, Vision in unified interface |
| **Enterprise Ready** | Teams, RBAC, Audit Logs, SSO, API Key Management |
| **HuggingFace Integration** | Direct model downloads with quantization selection |
| **Runtime Flexibility** | CUDA, Metal, Vulkan, CPU backend support |

### 1.3 Supported Model Types

- **Language Models (LLM)**: Llama, Mistral, Gemma, Qwen, DeepSeek
- **Diffusion Models**: Stable Diffusion 1.5, 2.1, 3.5, SDXL
- **Vision Models**: LLaVA, Qwen-VL, Gemma Vision
- **Audio Models**: Whisper, TTS models
- **Embedding Models**: BGE, E5, Instructor

---

## 2. Architecture

### 2.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    SnapLLM Desktop App (Tauri)                   │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   React Frontend                          │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │   │
│  │  │  Pages   │ │Components│ │  Hooks   │ │  Store   │   │   │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    API Layer                              │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │   │
│  │  │  REST    │ │WebSocket │ │  Tauri   │ │  Cache   │   │   │
│  │  │  Client  │ │  Stream  │ │  IPC     │ │  Layer   │   │   │
│  │  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    SnapLLM Backend Server                        │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │  Model   │ │  vPID    │ │  Cache   │ │  Inference       │   │
│  │  Manager │ │  Engine  │ │  System  │ │  Engine          │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────────┘   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
│  │  GGUF    │ │  CUDA    │ │  Metal   │ │  CPU (AVX/NEON)  │   │
│  │  Loader  │ │  Backend │ │  Backend │ │  Backend         │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Frontend Architecture

```
src/
├── components/
│   ├── layout/
│   │   ├── Layout.tsx          # Main app layout with sidebar
│   │   ├── Sidebar.tsx         # Navigation sidebar
│   │   └── Header.tsx          # Top header with search
│   └── ui/
│       ├── index.ts            # Unified exports
│       ├── Button.tsx          # Button variants
│       ├── Card.tsx            # Card container
│       ├── Badge.tsx           # Status badges
│       ├── Modal.tsx           # Modal dialogs
│       ├── Toggle.tsx          # Toggle switches
│       ├── Progress.tsx        # Progress bars
│       ├── Avatar.tsx          # User avatars
│       └── IconButton.tsx      # Icon-only buttons
├── pages/
│   ├── Dashboard.tsx           # Home dashboard
│   ├── Chat.tsx                # LLM chat interface
│   ├── Images.tsx              # Image generation
│   ├── Video.tsx               # Video generation
│   ├── Vision.tsx              # Multimodal vision
│   ├── Models.tsx              # Model management hub
│   ├── Compare.tsx             # A/B model comparison
│   ├── QuickSwitch.tsx         # vPID model switching
│   ├── RAG.tsx                 # Knowledge base manager
│   ├── Agents.tsx              # AI agents
│   ├── Workflows.tsx           # Visual workflow builder
│   ├── Playground.tsx          # API playground
│   ├── ApiKeys.tsx             # API key management
│   ├── Metrics.tsx             # Analytics dashboard
│   ├── Team.tsx                # Team management
│   ├── Audit.tsx               # Audit logging
│   ├── Security.tsx            # Security settings
│   ├── Settings.tsx            # App settings
│   └── Help.tsx                # Documentation
├── lib/
│   ├── api.ts                  # API client functions
│   └── utils.ts                # Utility functions
├── store/
│   └── index.ts                # Zustand store
├── App.tsx                     # Root component with router
├── main.tsx                    # Entry point
└── index.css                   # Global styles + Tailwind
```

### 2.3 vPID (Virtual Processing-In-Disk) Architecture

The vPID system enables sub-millisecond model switching through a 3-tier caching strategy:

```
┌─────────────────────────────────────────────────────────────┐
│                     vPID Cache System                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  HOT CACHE (GPU VRAM)                                │   │
│  │  • Currently active model                            │   │
│  │  • Instant inference (~0ms switch)                   │   │
│  │  • Capacity: 1 model                                 │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                          ▼                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  WARM CACHE (System RAM)                             │   │
│  │  • Recently used models                              │   │
│  │  • Fast switch (<1ms)                                │   │
│  │  • Capacity: 2-4 models                              │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                          ▼                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  COLD CACHE (Disk/SSD)                               │   │
│  │  • All loaded models                                 │   │
│  │  • Slower switch (~100-500ms)                        │   │
│  │  • Capacity: Unlimited                               │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Technology Stack

### 3.1 Frontend Technologies

| Technology | Version | Purpose |
|------------|---------|---------|
| **React** | 18.2+ | UI framework |
| **TypeScript** | 5.0+ | Type safety |
| **Vite** | 5.0+ | Build tool & dev server |
| **Tailwind CSS** | 3.4+ | Utility-first styling |
| **Framer Motion** | 10+ | Animations |
| **TanStack Query** | 5.0+ | Server state management |
| **Zustand** | 4.0+ | Client state management |
| **React Router** | 6.0+ | Client-side routing |
| **Lucide React** | Latest | Icon library |
| **clsx** | Latest | Conditional class names |

### 3.2 Desktop Technologies

| Technology | Version | Purpose |
|------------|---------|---------|
| **Tauri** | 2.0+ | Desktop runtime |
| **Rust** | 1.70+ | Backend logic |
| **WebView** | Native | UI rendering |

### 3.3 Backend Integration

| Technology | Purpose |
|------------|---------|
| **REST API** | Standard HTTP endpoints |
| **WebSocket** | Streaming responses |
| **Server-Sent Events** | Real-time updates |

---

## 4. Design System

### 4.1 Color Palette

```css
/* Brand Colors */
--brand-50:  #eef2ff;
--brand-100: #e0e7ff;
--brand-200: #c7d2fe;
--brand-300: #a5b4fc;
--brand-400: #818cf8;
--brand-500: #6366f1;  /* Primary */
--brand-600: #4f46e5;
--brand-700: #4338ca;
--brand-800: #3730a3;
--brand-900: #312e81;

/* AI Purple (Accent) */
--ai-purple: #8b5cf6;

/* Surface Colors (Light Mode) */
--surface-50:  #f8fafc;
--surface-100: #f1f5f9;
--surface-200: #e2e8f0;
--surface-300: #cbd5e1;
--surface-400: #94a3b8;
--surface-500: #64748b;
--surface-600: #475569;
--surface-700: #334155;
--surface-800: #1e293b;
--surface-900: #0f172a;

/* Semantic Colors */
--success-500: #22c55e;
--warning-500: #f59e0b;
--error-500:   #ef4444;
```

### 4.2 Typography

```css
/* Font Family */
font-family: 'Inter', system-ui, -apple-system, sans-serif;

/* Font Sizes */
--text-xs:   0.75rem;   /* 12px */
--text-sm:   0.875rem;  /* 14px */
--text-base: 1rem;      /* 16px */
--text-lg:   1.125rem;  /* 18px */
--text-xl:   1.25rem;   /* 20px */
--text-2xl:  1.5rem;    /* 24px */
--text-3xl:  1.875rem;  /* 30px */

/* Font Weights */
--font-normal:   400;
--font-medium:   500;
--font-semibold: 600;
--font-bold:     700;
```

### 4.3 Spacing System

```css
/* Base unit: 4px */
--space-1:  0.25rem;  /* 4px */
--space-2:  0.5rem;   /* 8px */
--space-3:  0.75rem;  /* 12px */
--space-4:  1rem;     /* 16px */
--space-5:  1.25rem;  /* 20px */
--space-6:  1.5rem;   /* 24px */
--space-8:  2rem;     /* 32px */
--space-10: 2.5rem;   /* 40px */
--space-12: 3rem;     /* 48px */
```

### 4.4 Border Radius

```css
--radius-sm:   0.25rem;  /* 4px */
--radius-md:   0.375rem; /* 6px */
--radius-lg:   0.5rem;   /* 8px */
--radius-xl:   0.75rem;  /* 12px */
--radius-2xl:  1rem;     /* 16px */
--radius-full: 9999px;   /* Circular */
```

### 4.5 Shadows

```css
/* Elevation levels */
--shadow-sm: 0 1px 2px 0 rgb(0 0 0 / 0.05);
--shadow:    0 1px 3px 0 rgb(0 0 0 / 0.1), 0 1px 2px -1px rgb(0 0 0 / 0.1);
--shadow-md: 0 4px 6px -1px rgb(0 0 0 / 0.1), 0 2px 4px -2px rgb(0 0 0 / 0.1);
--shadow-lg: 0 10px 15px -3px rgb(0 0 0 / 0.1), 0 4px 6px -4px rgb(0 0 0 / 0.1);
--shadow-xl: 0 20px 25px -5px rgb(0 0 0 / 0.1), 0 8px 10px -6px rgb(0 0 0 / 0.1);
```

---

## 5. Component Library

### 5.1 Button Component

```tsx
import { Button } from '../components/ui';

// Variants
<Button variant="primary">Primary</Button>
<Button variant="secondary">Secondary</Button>
<Button variant="ghost">Ghost</Button>
<Button variant="danger">Danger</Button>
<Button variant="success">Success</Button>

// Sizes
<Button size="sm">Small</Button>
<Button size="md">Medium</Button>
<Button size="lg">Large</Button>

// With Icon
<Button variant="primary">
  <PlusIcon className="w-4 h-4 mr-2" />
  Add Model
</Button>

// Loading State
<Button variant="primary" disabled>
  <Loader className="w-4 h-4 mr-2 animate-spin" />
  Loading...
</Button>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `variant` | `'primary' \| 'secondary' \| 'ghost' \| 'danger' \| 'success'` | `'primary'` | Button style variant |
| `size` | `'sm' \| 'md' \| 'lg'` | `'md'` | Button size |
| `disabled` | `boolean` | `false` | Disabled state |
| `className` | `string` | - | Additional CSS classes |
| `onClick` | `() => void` | - | Click handler |

### 5.2 Card Component

```tsx
import { Card } from '../components/ui';

// Basic Card
<Card className="p-6">
  <h3>Card Title</h3>
  <p>Card content goes here</p>
</Card>

// Hoverable Card
<Card className="p-6 card-hover">
  Hoverable content
</Card>

// With Gradient Border
<Card className="p-6 border-brand-200 dark:border-brand-800">
  Highlighted card
</Card>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `className` | `string` | - | Additional CSS classes |
| `children` | `ReactNode` | - | Card content |

### 5.3 Badge Component

```tsx
import { Badge } from '../components/ui';

// Variants
<Badge variant="default">Default</Badge>
<Badge variant="brand">Brand</Badge>
<Badge variant="success">Success</Badge>
<Badge variant="warning">Warning</Badge>
<Badge variant="error">Error</Badge>

// Sizes
<Badge size="sm">Small</Badge>
<Badge size="md">Medium</Badge>

// With Dot Indicator
<Badge variant="success" dot>Online</Badge>

// With Icon
<Badge variant="brand">
  <Zap className="w-3 h-3 mr-1" />
  GPU
</Badge>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `variant` | `'default' \| 'brand' \| 'success' \| 'warning' \| 'error'` | `'default'` | Badge style |
| `size` | `'sm' \| 'md'` | `'md'` | Badge size |
| `dot` | `boolean` | `false` | Show status dot |

### 5.4 Modal Component

```tsx
import { Modal } from '../components/ui';

const [isOpen, setIsOpen] = useState(false);

<Modal
  isOpen={isOpen}
  onClose={() => setIsOpen(false)}
  title="Modal Title"
  size="md"
>
  <div className="space-y-4">
    <p>Modal content goes here</p>
    <div className="flex justify-end gap-2">
      <Button variant="secondary" onClick={() => setIsOpen(false)}>
        Cancel
      </Button>
      <Button variant="primary">
        Confirm
      </Button>
    </div>
  </div>
</Modal>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `isOpen` | `boolean` | - | Modal visibility |
| `onClose` | `() => void` | - | Close handler |
| `title` | `string` | - | Modal title |
| `size` | `'sm' \| 'md' \| 'lg' \| 'xl'` | `'md'` | Modal width |

### 5.5 Progress Component

```tsx
import { Progress } from '../components/ui';

// Basic Progress
<Progress value={75} />

// Variants
<Progress value={75} variant="brand" />
<Progress value={75} variant="success" />
<Progress value={75} variant="warning" />
<Progress value={75} variant="error" />

// With Label
<div>
  <div className="flex justify-between mb-1">
    <span>Memory Usage</span>
    <span>75%</span>
  </div>
  <Progress value={75} variant="brand" />
</div>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `value` | `number` | `0` | Progress percentage (0-100) |
| `variant` | `'brand' \| 'success' \| 'warning' \| 'error'` | `'brand'` | Color variant |
| `className` | `string` | - | Additional CSS classes |

### 5.6 Toggle Component

```tsx
import { Toggle } from '../components/ui';

const [enabled, setEnabled] = useState(false);

<Toggle
  checked={enabled}
  onChange={setEnabled}
/>

// With Label
<div className="flex items-center justify-between">
  <span>Enable GPU Acceleration</span>
  <Toggle checked={enabled} onChange={setEnabled} />
</div>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `checked` | `boolean` | - | Toggle state |
| `onChange` | `(value: boolean) => void` | - | Change handler |
| `disabled` | `boolean` | `false` | Disabled state |

### 5.7 Avatar Component

```tsx
import { Avatar } from '../components/ui';

// With Image
<Avatar src="/user.jpg" name="John Doe" size="md" />

// Fallback to Initials
<Avatar name="John Doe" size="md" />

// Sizes
<Avatar name="JD" size="sm" />  {/* 32px */}
<Avatar name="JD" size="md" />  {/* 40px */}
<Avatar name="JD" size="lg" />  {/* 48px */}
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `src` | `string` | - | Image URL |
| `name` | `string` | - | User name for initials fallback |
| `size` | `'sm' \| 'md' \| 'lg'` | `'md'` | Avatar size |

### 5.8 IconButton Component

```tsx
import { IconButton } from '../components/ui';

<IconButton
  icon={<Settings className="w-4 h-4" />}
  label="Settings"
  onClick={() => {}}
/>

// With Tooltip
<IconButton
  icon={<Trash2 className="w-4 h-4 text-error-500" />}
  label="Delete"
  onClick={handleDelete}
/>
```

**Props:**

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `icon` | `ReactNode` | - | Icon element |
| `label` | `string` | - | Accessibility label |
| `onClick` | `() => void` | - | Click handler |

---

## 6. Pages & Features

### 6.1 Dashboard (`Dashboard.tsx`)

**Purpose:** Landing page with system overview and quick actions.

**Features:**
- System health status cards
- Active models summary
- Recent activity feed
- Quick action buttons
- Performance sparklines

### 6.2 Chat Interface (`Chat.tsx`)

**Purpose:** Interactive LLM chat with conversation management.

**Features:**
- Multi-turn conversation
- Conversation history sidebar
- System prompt presets
- Generation settings panel (temperature, max tokens, top-p, top-k)
- Model selection dropdown
- Streaming response display
- Copy/regenerate messages
- Export conversations

**Key Components:**
```tsx
// Message structure
interface Message {
  id: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  timestamp: Date;
  model?: string;
  tokens?: number;
  latency?: number;
}

// Conversation structure
interface Conversation {
  id: string;
  title: string;
  messages: Message[];
  model: string;
  createdAt: Date;
  updatedAt: Date;
}
```

### 6.3 Image Generation (`Images.tsx`)

**Purpose:** Text-to-image generation with Stable Diffusion models.

**Features:**
- Prompt input with negative prompt support
- Aspect ratio presets (1:1, 16:9, 9:16, 4:3, 3:4)
- Generation settings:
  - Steps (10-50)
  - CFG Scale (1-20)
  - Seed control
  - Sampler selection (Euler, DPM++, DDIM, etc.)
- Style presets (Photorealistic, Anime, Digital Art, etc.)
- Image preview with zoom
- Generation history sidebar
- Download/share options

### 6.4 Video Generation (`Video.tsx`)

**Purpose:** Text-to-video and image-to-video generation.

**Features:**
- Text prompt input
- Reference image upload
- Duration presets (2s, 4s, 8s)
- Resolution options (256p, 512p, 720p)
- Motion strength control
- Frame rate selection
- Video preview with playback controls
- Generation progress tracking

### 6.5 Vision/Multimodal (`Vision.tsx`)

**Purpose:** Image understanding and analysis with vision models.

**Features:**
- Image upload via drag-and-drop
- Quick analysis templates:
  - Describe Image
  - Identify Objects
  - Extract Text (OCR)
  - Analyze Composition
- Chat interface for follow-up questions
- Multi-image comparison
- Analysis history

### 6.6 Model Hub (`Models.tsx`)

**Purpose:** Comprehensive model management with precise specifications.

**Features:**

#### Tab: Loaded Models
- Active model cards with live stats
- Performance metrics (throughput, latency, memory)
- Architecture visualization
- Unload/reload controls

#### Tab: Local Models
- Scanned models folder display
- Quick load functionality
- File path information

#### Tab: HuggingFace Hub
- Popular models browsing
- Search and filter
- Download with quantization selection
- Progress tracking

#### Model Specifications Panel
When clicking on a loaded model, a detailed panel slides in with:

**Overview Tab:**
- Parameters count (e.g., "8.03B")
- Context length (e.g., "131,072 tokens")
- File size and format
- Capabilities list
- Live statistics (memory, throughput, latency, requests/hr)

**Architecture Tab:**
- Model architecture type (e.g., "LlamaForCausalLM")
- Number of layers
- Attention heads count
- Hidden size
- Vocabulary size
- Visual data flow diagram

**Performance Tab:**
- Prompt processing speed
- Token generation speed
- Time to first token
- Quantization impact comparison

**Requirements Tab:**
- Minimum/Recommended RAM
- GPU VRAM requirements
- Storage requirements
- Supported backends
- License information

#### Runtime Configuration Modal
- Backend selection (CUDA, Metal, Vulkan, CPU)
- GPU layers slider
- CPU threads configuration
- Context size selection
- Advanced options:
  - Flash Attention toggle
  - Memory mapping (mmap)
  - Memory lock (mlock)

### 6.7 Model Comparison (`Compare.tsx`)

**Purpose:** A/B testing between multiple models.

**Features:**
- Select 2-4 models for comparison
- Side-by-side output display
- Identical prompt testing
- Voting system (prefer A/B/tie)
- Performance metrics comparison
- Win/loss tracking
- Comparison history

### 6.8 Quick Switch (`QuickSwitch.tsx`)

**Purpose:** Demonstrate vPID sub-millisecond model switching.

**Features:**
- Model list with cache tier indicators (HOT/WARM/COLD)
- One-click model activation
- Switch time display
- Performance history graph
- vPID technology explanation

### 6.9 RAG Knowledge Base (`RAG.tsx`)

**Purpose:** Document ingestion and retrieval-augmented generation.

**Features:**

#### Knowledge Base Management
- Create/delete knowledge bases
- Configure embedding model
- Set chunking parameters

#### Document Ingestion
- Drag-and-drop upload
- Supported formats: PDF, TXT, MD, DOCX, HTML
- Processing status tracking
- Chunk preview

#### Retrieval Testing
- Query input
- Top-K results display
- Relevance scores
- Source highlighting

### 6.10 AI Agents (`Agents.tsx`)

**Purpose:** Create and manage autonomous AI agents.

**Features:**
- Agent templates (Research, Code, Writing, Data)
- Capability configuration:
  - Text processing
  - Vision analysis
  - Code execution
  - Web browsing
  - File operations
- Tool assignment
- Execution monitoring
- Agent statistics

### 6.11 Workflow Builder (`Workflows.tsx`)

**Purpose:** Visual AI pipeline orchestration.

**Features:**
- Drag-and-drop workflow editor
- Node types:
  - Input nodes (Text, Image, File)
  - Processing nodes (LLM, Vision, Embedding)
  - Logic nodes (Condition, Loop, Switch)
  - Output nodes (Text, Image, File, API)
- Connection management
- Template gallery
- Execution history
- Export/import workflows

### 6.12 API Playground (`Playground.tsx`)

**Purpose:** Interactive API testing and code generation.

**Features:**

#### Endpoint Categories
- Chat Completions
- Image Generation
- Video Generation
- Vision Analysis
- Audio Processing
- Embeddings
- Model Management

#### Request Builder
- Parameter configuration
- JSON body editor
- Header management

#### Response Viewer
- Formatted JSON display
- Response time tracking
- Token usage statistics

#### Code Snippets
- Auto-generated code in:
  - cURL
  - Python
  - JavaScript/TypeScript
  - Go
  - Rust

### 6.13 API Keys (`ApiKeys.tsx`)

**Purpose:** Secure API key management.

**Features:**
- Key generation with permissions
- Rate limit configuration
- Usage tracking
- Expiration settings
- Key revocation
- Activity logs

### 6.14 Analytics Dashboard (`Metrics.tsx`)

**Purpose:** Real-time performance monitoring and observability.

**Features:**

#### Top Metrics Cards
- Server uptime
- Requests per hour
- Average latency
- Total throughput
- Trend indicators (+/-%)

#### Charts
- Throughput sparkline (30-minute window)
- Latency sparkline
- Usage by type donut chart
- Memory usage trend

#### Top Models Table
- Ranked by request count
- Latency and throughput columns
- Device indicators

#### System Resources
- Memory usage with progress bar
- GPU utilization
- CPU usage
- Disk usage with breakdown

#### Alerts Panel
- Recent alerts with severity
- Timestamp and message
- Alert type categorization

#### Model Performance Table
- Detailed per-model metrics
- Status indicators
- Sortable columns
- CSV export

### 6.15 Team Management (`Team.tsx`)

**Purpose:** User and team administration.

**Features:**
- Member list with roles
- Role types:
  - Owner (full access)
  - Admin (team management)
  - Member (API access)
  - Viewer (read-only)
- Invite new members
- Edit permissions
- Remove members
- Activity tracking
- API usage per member

### 6.16 Audit Log (`Audit.tsx`)

**Purpose:** Security and compliance logging.

**Features:**
- Event categories:
  - Authentication events
  - API requests
  - Model operations
  - Configuration changes
  - Security events
  - User management
- Search and filter
- Date range selection
- Event details view
- Export functionality

### 6.17 Security Settings (`Security.tsx`)

**Purpose:** Security configuration and access controls.

**Features:**

#### Security Score
- Overall security rating
- Enabled/disabled features count

#### Settings Categories

**Authentication:**
- Two-factor authentication
- Single Sign-On (SSO)

**API Security:**
- API key authentication
- Rate limiting

**Network:**
- IP whitelist
- CORS configuration

**Data Protection:**
- Encryption at rest
- Audit logging
- Model access controls

#### Active Sessions
- Current device indicator
- Device and browser info
- Location and IP
- Last active time
- Session revocation

### 6.18 Help & Documentation (`Help.tsx`)

**Purpose:** In-app documentation and support.

**Features:**
- Search documentation
- Getting started guides
- Feature guides
- FAQ accordion
- Quick links (API docs, GitHub, Discord)
- Contact support

---

## 7. API Integration

### 7.1 API Client (`lib/api.ts`)

```typescript
// Base configuration
const API_BASE_URL = 'http://localhost:8080';

// Model Management
export async function listModels(): Promise<{ models: Model[] }>;
export async function loadModel(params: LoadModelParams): Promise<void>;
export async function unloadModel(modelId: string): Promise<void>;
export async function scanModelsFolder(): Promise<string[]>;

// Chat Completions
export async function sendChatMessage(params: ChatParams): Promise<ChatResponse>;
export async function streamChatMessage(params: ChatParams): AsyncGenerator<string>;

// Image Generation
export async function generateImage(params: ImageParams): Promise<ImageResponse>;

// Vision
export async function analyzeImage(params: VisionParams): Promise<VisionResponse>;

// Embeddings
export async function createEmbedding(params: EmbeddingParams): Promise<number[]>;

// Server Status
export async function getServerStatus(): Promise<ServerStatus>;
export async function getCacheStats(): Promise<CacheStats>;
```

### 7.2 API Types

```typescript
interface Model {
  id: string;
  name: string;
  engine: string;
  device: string;
  status: 'ready' | 'loading' | 'error';
  ram_usage_mb: number;
  strategy?: string;
  requests_per_hour: number;
  avg_latency_ms: number;
  throughput_toks: number;
  loaded_at: string;
}

interface LoadModelParams {
  model_id: string;
  file_path: string;
  strategy?: 'balanced' | 'aggressive' | 'conservative';
}

interface ChatParams {
  model: string;
  messages: Array<{
    role: 'user' | 'assistant' | 'system';
    content: string;
  }>;
  temperature?: number;
  max_tokens?: number;
  top_p?: number;
  stream?: boolean;
}

interface ServerStatus {
  version: string;
  uptime_seconds: number;
  port: number;
  gpu_enabled: boolean;
  models_loaded: number;
}
```

### 7.3 TanStack Query Integration

```typescript
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';

// Fetching models
const { data: models, isLoading } = useQuery({
  queryKey: ['models'],
  queryFn: listModels,
  refetchInterval: 5000, // Auto-refresh every 5s
});

// Loading a model
const queryClient = useQueryClient();
const loadMutation = useMutation({
  mutationFn: loadModel,
  onSuccess: () => {
    queryClient.invalidateQueries({ queryKey: ['models'] });
  },
});

// Usage
loadMutation.mutate({
  model_id: 'llama-3-8b',
  file_path: '/models/llama-3-8b.gguf',
  strategy: 'balanced',
});
```

---

## 8. State Management

### 8.1 Zustand Store

```typescript
// store/index.ts
import { create } from 'zustand';
import { persist } from 'zustand/middleware';

interface AppState {
  // Theme
  theme: 'light' | 'dark' | 'system';
  setTheme: (theme: 'light' | 'dark' | 'system') => void;

  // Sidebar
  sidebarCollapsed: boolean;
  toggleSidebar: () => void;

  // Active Model
  activeModel: string | null;
  setActiveModel: (modelId: string | null) => void;

  // Chat
  conversations: Conversation[];
  activeConversation: string | null;
  addConversation: (conv: Conversation) => void;
  setActiveConversation: (id: string) => void;

  // Settings
  settings: AppSettings;
  updateSettings: (settings: Partial<AppSettings>) => void;
}

export const useAppStore = create<AppState>()(
  persist(
    (set) => ({
      theme: 'system',
      setTheme: (theme) => set({ theme }),

      sidebarCollapsed: false,
      toggleSidebar: () => set((s) => ({ sidebarCollapsed: !s.sidebarCollapsed })),

      activeModel: null,
      setActiveModel: (modelId) => set({ activeModel: modelId }),

      conversations: [],
      activeConversation: null,
      addConversation: (conv) => set((s) => ({
        conversations: [conv, ...s.conversations],
      })),
      setActiveConversation: (id) => set({ activeConversation: id }),

      settings: defaultSettings,
      updateSettings: (newSettings) => set((s) => ({
        settings: { ...s.settings, ...newSettings },
      })),
    }),
    {
      name: 'snapllm-storage',
      partialize: (state) => ({
        theme: state.theme,
        sidebarCollapsed: state.sidebarCollapsed,
        settings: state.settings,
      }),
    }
  )
);
```

### 8.2 Usage Example

```tsx
import { useAppStore } from '../store';

function ModelSelector() {
  const activeModel = useAppStore((s) => s.activeModel);
  const setActiveModel = useAppStore((s) => s.setActiveModel);

  return (
    <select
      value={activeModel || ''}
      onChange={(e) => setActiveModel(e.target.value)}
    >
      {/* options */}
    </select>
  );
}
```

---

## 9. Routing

### 9.1 Route Configuration

```tsx
// App.tsx
import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { Layout } from './components/layout/Layout';

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route element={<Layout />}>
          {/* Dashboard */}
          <Route path="/" element={<Dashboard />} />

          {/* GenAI Studio */}
          <Route path="/chat" element={<Chat />} />
          <Route path="/images" element={<Images />} />
          <Route path="/video" element={<Video />} />
          <Route path="/vision" element={<Vision />} />

          {/* Model Hub */}
          <Route path="/models" element={<Models />} />
          <Route path="/compare" element={<Compare />} />
          <Route path="/switch" element={<QuickSwitch />} />

          {/* Advanced */}
          <Route path="/rag" element={<RAG />} />
          <Route path="/agents" element={<Agents />} />
          <Route path="/workflows" element={<Workflows />} />

          {/* Developer */}
          <Route path="/playground" element={<Playground />} />
          <Route path="/api-keys" element={<ApiKeys />} />
          <Route path="/metrics" element={<Metrics />} />

          {/* Enterprise */}
          <Route path="/team" element={<Team />} />
          <Route path="/audit" element={<Audit />} />
          <Route path="/security" element={<Security />} />

          {/* Settings */}
          <Route path="/settings" element={<Settings />} />
          <Route path="/help" element={<Help />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
```

### 9.2 Navigation Structure

```
├── Dashboard (/)
│
├── GenAI Studio
│   ├── Chat (/chat)
│   ├── Images (/images)
│   ├── Video (/video)
│   └── Vision (/vision)
│
├── Model Hub
│   ├── Models (/models)
│   ├── Compare (/compare)
│   └── Quick Switch (/switch)
│
├── Advanced
│   ├── RAG (/rag)
│   ├── Agents (/agents)
│   └── Workflows (/workflows)
│
├── Developer
│   ├── Playground (/playground)
│   ├── API Keys (/api-keys)
│   └── Metrics (/metrics)
│
├── Enterprise
│   ├── Team (/team)
│   ├── Audit (/audit)
│   └── Security (/security)
│
└── Settings
    ├── Settings (/settings)
    └── Help (/help)
```

---

## 10. Development Guide

### 10.1 Prerequisites

```bash
# Required software
- Node.js 18+
- npm or yarn or pnpm
- Rust 1.70+ (for Tauri desktop)
- Git
```

### 10.2 Installation

```bash
# Clone repository
git clone https://github.com/snapllm/snapllm.git
cd snapllm/desktop-app

# Install dependencies
npm install

# Start development server
npm run dev

# Build for production
npm run build

# Build desktop app (Tauri)
npm run tauri build
```

### 10.3 Project Scripts

```json
{
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview",
    "lint": "eslint src --ext ts,tsx",
    "format": "prettier --write src/**/*.{ts,tsx}",
    "tauri": "tauri",
    "tauri:dev": "tauri dev",
    "tauri:build": "tauri build"
  }
}
```

### 10.4 Environment Variables

```env
# .env.local
VITE_API_URL=http://localhost:8080
VITE_WS_URL=ws://localhost:8080
VITE_APP_NAME=SnapLLM
VITE_APP_VERSION=1.0.0
```

### 10.5 Code Style Guidelines

#### TypeScript
- Use strict mode
- Prefer `interface` over `type` for object shapes
- Use explicit return types for functions
- Avoid `any` type

#### React
- Use functional components with hooks
- Prefer named exports
- Co-locate related files
- Use `clsx` for conditional classes

#### CSS/Tailwind
- Follow utility-first approach
- Use design system tokens
- Extract repeated patterns to components
- Support dark mode with `dark:` variants

### 10.6 Adding a New Page

```tsx
// 1. Create page file: src/pages/NewPage.tsx
import React from 'react';
import { Card, Button } from '../components/ui';

export default function NewPage() {
  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Page Title
          </h1>
          <p className="text-surface-500 mt-1">
            Page description
          </p>
        </div>
        <Button variant="primary">
          Action
        </Button>
      </div>

      <Card className="p-6">
        {/* Page content */}
      </Card>
    </div>
  );
}

// 2. Add route in App.tsx
<Route path="/new-page" element={<NewPage />} />

// 3. Add navigation item in Layout.tsx sidebar
```

---

## 11. Deployment

### 11.1 Web Deployment

```bash
# Build production bundle
npm run build

# Output in dist/ folder
# Deploy to any static hosting:
# - Vercel
# - Netlify
# - AWS S3 + CloudFront
# - GitHub Pages
```

### 11.2 Desktop Deployment (Tauri)

```bash
# Build for current platform
npm run tauri build

# Output locations:
# - Windows: src-tauri/target/release/bundle/msi/
# - macOS: src-tauri/target/release/bundle/dmg/
# - Linux: src-tauri/target/release/bundle/deb/
```

### 11.3 Docker Deployment

```dockerfile
# Dockerfile
FROM node:18-alpine AS builder
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build

FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
```

```bash
# Build and run
docker build -t snapllm-ui .
docker run -p 80:80 snapllm-ui
```

---

## Appendix A: Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl/Cmd + K` | Open command palette |
| `Ctrl/Cmd + N` | New conversation |
| `Ctrl/Cmd + Enter` | Send message |
| `Ctrl/Cmd + Shift + N` | Generate image |
| `Ctrl/Cmd + /` | Toggle sidebar |
| `Ctrl/Cmd + ,` | Open settings |
| `Escape` | Close modal/panel |

---

## Appendix B: Model Specifications Reference

### Llama 3 Family

| Model | Parameters | Context | Vocab | Hidden | Layers | Heads |
|-------|------------|---------|-------|--------|--------|-------|
| Llama 3.2 1B | 1.24B | 131K | 128K | 2048 | 16 | 32 |
| Llama 3.2 3B | 3.21B | 131K | 128K | 3072 | 28 | 24 |
| Llama 3.1 8B | 8.03B | 131K | 128K | 4096 | 32 | 32 |
| Llama 3.1 70B | 70.6B | 131K | 128K | 8192 | 80 | 64 |

### Mistral Family

| Model | Parameters | Context | Vocab | Hidden | Layers | Heads |
|-------|------------|---------|-------|--------|--------|-------|
| Mistral 7B | 7.24B | 32K | 32K | 4096 | 32 | 32 |
| Mixtral 8x7B | 46.7B | 32K | 32K | 4096 | 32 | 32 |

### Gemma Family

| Model | Parameters | Context | Vocab | Hidden | Layers | Heads |
|-------|------------|---------|-------|--------|--------|-------|
| Gemma 2 2B | 2.61B | 8K | 256K | 2304 | 26 | 8 |
| Gemma 2 9B | 9.24B | 8K | 256K | 3584 | 42 | 16 |
| Gemma 2 27B | 27.2B | 8K | 256K | 4608 | 46 | 32 |

### Qwen Family

| Model | Parameters | Context | Vocab | Hidden | Layers | Heads |
|-------|------------|---------|-------|--------|--------|-------|
| Qwen 2.5 3B | 3.09B | 131K | 152K | 2048 | 36 | 16 |
| Qwen 2.5 7B | 7.61B | 131K | 152K | 3584 | 28 | 28 |
| Qwen 2.5 72B | 72.7B | 131K | 152K | 8192 | 80 | 64 |

---

## Appendix C: Quantization Guide

| Format | Bits | Quality | Speed | VRAM | Use Case |
|--------|------|---------|-------|------|----------|
| F16 | 16 | Best | Slow | High | Research |
| Q8_0 | 8 | Excellent | Medium | Medium | Quality-focused |
| Q6_K | 6 | Very Good | Good | Medium | Balanced |
| Q5_K_M | 5 | Good | Fast | Low | Recommended |
| Q4_K_M | 4 | Good | Fastest | Lowest | Speed-focused |
| Q4_0 | 4 | Acceptable | Fastest | Lowest | Minimum |

---

## Appendix D: Troubleshooting

### Common Issues

**Model fails to load:**
- Check available RAM/VRAM
- Verify file path and format
- Try lower quantization
- Check backend compatibility

**Slow inference:**
- Enable GPU acceleration
- Increase GPU layers
- Check for thermal throttling
- Try smaller context size

**UI not updating:**
- Check API server connection
- Verify WebSocket connectivity
- Clear browser cache
- Check console for errors

**Connection refused:**
- Ensure backend server is running
- Check port configuration
- Verify firewall settings
- Check CORS configuration

---

## Appendix E: License

SnapLLM Enterprise UI Suite is licensed under the MIT License.

```
MIT License

Copyright (c) 2024 SnapLLM

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
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

**Documentation Version:** 1.0.0
**Generated:** December 2024
**Maintained by:** SnapLLM Team


