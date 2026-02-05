// ============================================================================
// SnapLLM Enterprise - Advanced Model Management Hub
// Runtime Selection, Model Graphs, vPID Management
// ============================================================================

import React, { useState, useEffect } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Cpu,
  HardDrive,
  Upload,
  Trash2,
  Plus,
  Search,
  Filter,
  Settings2,
  Play,
  Pause,
  RefreshCw,
  Zap,
  Activity,
  Database,
  GitBranch,
  Layers,
  Box,
  Server,
  CheckCircle2,
  XCircle,
  AlertTriangle,
  AlertCircle,
  Clock,
  TrendingUp,
  BarChart3,
  Eye,
  EyeOff,
  Copy,
  Check,
  FolderOpen,
  FileText,
  Loader,
  ChevronRight,
  ChevronDown,
  Sparkles,
  Gauge,
  MemoryStick,
  CircuitBoard,
  Network,
  Binary,
  Layers3,
  MoreVertical,
  Info,
  ArrowUpDown,
} from 'lucide-react';
import {
  listModels,
  loadModel,
  unloadModel,
  scanModelsFolder,
  selectModelFile,
  selectModelsFolder,
  scanFolder,
  copyModelToFolder,
  getCacheStats,
  clearCache,
  getConfig,
  getDefaultModelsPath,
  getWorkspaceFolders,
  deleteWorkspaceFolder,
  getDefaultWorkspacePath,
  WorkspaceFolderInfo,
  isTauriAvailable,
  detectModelType,
  getModelTypeLabel,
  getModelTypeBadgeColor,
  ModelType,
} from '../lib/api';
import { Button, IconButton, Badge, Card, Modal, Progress, Toggle } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface LoadedModel {
  id: string;
  name: string;
  engine: string;
  device: string;
  status: string;
  ram_usage_mb: number;
  strategy?: string;
  requests_per_hour: number;
  avg_latency_ms: number;
  throughput_toks: number;
  loaded_at: string;
}

interface ModelSpecifications {
  parameters: string;
  architecture: string;
  quantization: string;
  contextLength: number;
  vocabSize: number;
  hiddenSize: number;
  numLayers: number;
  numHeads: number;
  fileSize: string;
  format: string;
  creator: string;
  license: string;
  capabilities: string[];
  memoryRequirements: {
    minimum: string;
    recommended: string;
    vram: string;
  };
  performance: {
    promptSpeed: string;
    genSpeed: string;
    firstToken: string;
  };
}

interface RuntimeConfig {
  backend: 'cpu' | 'cuda' | 'metal' | 'vulkan' | 'opencl';
  gpuLayers: number;
  threads: number;
  batchSize: number;
  contextSize: number;
  flashAttention: boolean;
  mmap: boolean;
  mlock: boolean;
}

const RUNTIME_BACKENDS = [
  { id: 'cuda', name: 'NVIDIA CUDA', icon: Zap, description: 'Best performance on NVIDIA GPUs', color: 'success' },
  { id: 'metal', name: 'Apple Metal', icon: Sparkles, description: 'Optimized for Apple Silicon', color: 'brand' },
  { id: 'vulkan', name: 'Vulkan', icon: Layers3, description: 'Cross-platform GPU acceleration', color: 'warning' },
  { id: 'cpu', name: 'CPU Only', icon: Cpu, description: 'Universal compatibility', color: 'default' },
];

// ============================================================================
// Helper: Get Precise Model Specifications
// ============================================================================

function getModelSpecifications(model: LoadedModel): ModelSpecifications {
  const name = model.name.toLowerCase();

  // Llama models
  if (name.includes('llama-3') || name.includes('llama3')) {
    const is8B = name.includes('8b');
    const is70B = name.includes('70b');
    return {
      parameters: is70B ? '70.6B' : is8B ? '8.03B' : '3.21B',
      architecture: 'LlamaForCausalLM (Decoder-Only Transformer)',
      quantization: name.includes('q4_k_m') ? 'Q4_K_M (4-bit)' : name.includes('q8') ? 'Q8_0 (8-bit)' : name.includes('f16') ? 'FP16' : 'Q4_K_S (4-bit)',
      contextLength: 131072,
      vocabSize: 128256,
      hiddenSize: is70B ? 8192 : is8B ? 4096 : 3072,
      numLayers: is70B ? 80 : is8B ? 32 : 28,
      numHeads: is70B ? 64 : is8B ? 32 : 24,
      fileSize: is70B ? '39.5 GB' : is8B ? '4.7 GB' : '1.9 GB',
      format: 'GGUF',
      creator: 'Meta AI',
      license: 'Llama 3.2 Community License',
      capabilities: ['Text Generation', 'Chat', 'Code', 'Reasoning', 'Multi-turn Dialogue', 'Function Calling'],
      memoryRequirements: {
        minimum: is70B ? '48 GB' : is8B ? '6 GB' : '3 GB',
        recommended: is70B ? '64 GB' : is8B ? '8 GB' : '4 GB',
        vram: is70B ? '40 GB' : is8B ? '6 GB' : '3 GB',
      },
      performance: {
        promptSpeed: is70B ? '~45 tok/s' : is8B ? '~120 tok/s' : '~200 tok/s',
        genSpeed: is70B ? '~15 tok/s' : is8B ? '~45 tok/s' : '~80 tok/s',
        firstToken: is70B ? '~2.5s' : is8B ? '~0.8s' : '~0.3s',
      },
    };
  }

  // Mistral models
  if (name.includes('mistral')) {
    return {
      parameters: '7.24B',
      architecture: 'MistralForCausalLM (Sliding Window Attention)',
      quantization: name.includes('q4') ? 'Q4_K_M (4-bit)' : name.includes('q8') ? 'Q8_0 (8-bit)' : 'Q5_K_M (5-bit)',
      contextLength: 32768,
      vocabSize: 32000,
      hiddenSize: 4096,
      numLayers: 32,
      numHeads: 32,
      fileSize: '4.1 GB',
      format: 'GGUF',
      creator: 'Mistral AI',
      license: 'Apache 2.0',
      capabilities: ['Text Generation', 'Chat', 'Code', 'Reasoning', 'Sliding Window Attention'],
      memoryRequirements: {
        minimum: '5 GB',
        recommended: '8 GB',
        vram: '6 GB',
      },
      performance: {
        promptSpeed: '~150 tok/s',
        genSpeed: '~50 tok/s',
        firstToken: '~0.5s',
      },
    };
  }

  // Gemma models
  if (name.includes('gemma')) {
    const is27B = name.includes('27b');
    const is9B = name.includes('9b');
    return {
      parameters: is27B ? '27.2B' : is9B ? '9.24B' : '4.21B',
      architecture: 'Gemma2ForCausalLM (Multi-Query Attention)',
      quantization: name.includes('q4') ? 'Q4_K_M (4-bit)' : name.includes('q8') ? 'Q8_0 (8-bit)' : 'Q5_K_M (5-bit)',
      contextLength: 8192,
      vocabSize: 256000,
      hiddenSize: is27B ? 4608 : is9B ? 3584 : 2560,
      numLayers: is27B ? 46 : is9B ? 42 : 32,
      numHeads: is27B ? 32 : is9B ? 16 : 16,
      fileSize: is27B ? '15.2 GB' : is9B ? '5.4 GB' : '2.5 GB',
      format: 'GGUF',
      creator: 'Google DeepMind',
      license: 'Gemma Terms of Use',
      capabilities: ['Text Generation', 'Chat', 'Code', 'Reasoning', 'Multi-Query Attention'],
      memoryRequirements: {
        minimum: is27B ? '20 GB' : is9B ? '7 GB' : '4 GB',
        recommended: is27B ? '32 GB' : is9B ? '10 GB' : '6 GB',
        vram: is27B ? '16 GB' : is9B ? '8 GB' : '4 GB',
      },
      performance: {
        promptSpeed: is27B ? '~80 tok/s' : is9B ? '~110 tok/s' : '~180 tok/s',
        genSpeed: is27B ? '~25 tok/s' : is9B ? '~40 tok/s' : '~70 tok/s',
        firstToken: is27B ? '~1.5s' : is9B ? '~0.7s' : '~0.4s',
      },
    };
  }

  // Qwen models
  if (name.includes('qwen')) {
    const is72B = name.includes('72b');
    const is7B = name.includes('7b');
    return {
      parameters: is72B ? '72.7B' : is7B ? '7.61B' : '3.09B',
      architecture: 'Qwen2ForCausalLM (Grouped Query Attention)',
      quantization: name.includes('q4') ? 'Q4_K_M (4-bit)' : name.includes('q8') ? 'Q8_0 (8-bit)' : 'Q5_K_M (5-bit)',
      contextLength: 131072,
      vocabSize: 152064,
      hiddenSize: is72B ? 8192 : is7B ? 3584 : 2048,
      numLayers: is72B ? 80 : is7B ? 28 : 36,
      numHeads: is72B ? 64 : is7B ? 28 : 16,
      fileSize: is72B ? '41.0 GB' : is7B ? '4.4 GB' : '1.8 GB',
      format: 'GGUF',
      creator: 'Alibaba Cloud',
      license: 'Qwen License (Apache 2.0 compatible)',
      capabilities: ['Text Generation', 'Chat', 'Code', 'Multilingual (100+ languages)', 'Math', 'Tool Use'],
      memoryRequirements: {
        minimum: is72B ? '48 GB' : is7B ? '6 GB' : '3 GB',
        recommended: is72B ? '64 GB' : is7B ? '8 GB' : '4 GB',
        vram: is72B ? '40 GB' : is7B ? '6 GB' : '3 GB',
      },
      performance: {
        promptSpeed: is72B ? '~40 tok/s' : is7B ? '~130 tok/s' : '~210 tok/s',
        genSpeed: is72B ? '~12 tok/s' : is7B ? '~48 tok/s' : '~85 tok/s',
        firstToken: is72B ? '~3.0s' : is7B ? '~0.6s' : '~0.25s',
      },
    };
  }

  // Stable Diffusion models
  if (name.includes('stable') || name.includes('sd') || name.includes('diffusion')) {
    return {
      parameters: '2.6B',
      architecture: 'StableDiffusion3Pipeline (DiT + CLIP + VAE)',
      quantization: name.includes('q4') ? 'Q4_0 (4-bit)' : 'Q8_0 (8-bit)',
      contextLength: 77,
      vocabSize: 49408,
      hiddenSize: 1536,
      numLayers: 24,
      numHeads: 24,
      fileSize: '4.2 GB',
      format: 'GGUF',
      creator: 'Stability AI',
      license: 'Stability AI Community License',
      capabilities: ['Text-to-Image', 'Image-to-Image', 'Inpainting', 'ControlNet', 'LoRA Support'],
      memoryRequirements: {
        minimum: '6 GB',
        recommended: '8 GB',
        vram: '6 GB',
      },
      performance: {
        promptSpeed: 'N/A',
        genSpeed: '~2.5 img/min (512x512)',
        firstToken: '~3s init',
      },
    };
  }

  // Default fallback
  return {
    parameters: 'Unknown',
    architecture: 'Transformer',
    quantization: name.includes('q4') ? 'Q4_K_M (4-bit)' : name.includes('q8') ? 'Q8_0 (8-bit)' : 'Unknown',
    contextLength: 4096,
    vocabSize: 32000,
    hiddenSize: 2048,
    numLayers: 24,
    numHeads: 16,
    fileSize: `${((model.ram_usage_mb || 0) / 1024).toFixed(1)} GB`,
    format: 'GGUF',
    creator: 'Unknown',
    license: 'Check model card',
    capabilities: ['Text Generation'],
    memoryRequirements: {
      minimum: `${((model.ram_usage_mb || 0) / 1024 * 0.8).toFixed(1)} GB`,
      recommended: `${((model.ram_usage_mb || 0) / 1024 * 1.2).toFixed(1)} GB`,
      vram: `${((model.ram_usage_mb || 0) / 1024).toFixed(1)} GB`,
    },
    performance: {
      promptSpeed: `~${(model.throughput_toks || 0) * 2} tok/s`,
      genSpeed: `~${model.throughput_toks || 0} tok/s`,
      firstToken: `~${((model.avg_latency_ms || 0) / 1000).toFixed(2)}s`,
    },
  };
}

// ============================================================================
// Model Specifications Panel Component
// ============================================================================

function ModelSpecificationsPanel({
  model,
  onClose,
}: {
  model: LoadedModel;
  onClose: () => void;
}) {
  const specs = getModelSpecifications(model);
  const [activeSpecTab, setActiveSpecTab] = useState<'overview' | 'architecture' | 'performance' | 'requirements'>('overview');

  return (
    <motion.div
      initial={{ opacity: 0, x: 300 }}
      animate={{ opacity: 1, x: 0 }}
      exit={{ opacity: 0, x: 300 }}
      className="fixed right-0 top-0 bottom-0 w-[480px] bg-white dark:bg-surface-900 border-l border-surface-200 dark:border-surface-700 shadow-2xl z-50 overflow-y-auto"
    >
      {/* Header */}
      <div className="sticky top-0 bg-white dark:bg-surface-900 border-b border-surface-200 dark:border-surface-700 p-4 z-10">
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
              <Cpu className="w-5 h-5 text-white" />
            </div>
            <div>
              <h2 className="font-semibold text-surface-900 dark:text-white">{model.name}</h2>
              <p className="text-xs text-surface-500">{specs.creator} • {specs.license}</p>
            </div>
          </div>
          <IconButton
            icon={<XCircle className="w-5 h-5" />}
            label="Close"
            onClick={onClose}
          />
        </div>

        {/* Tab Navigation */}
        <div className="flex items-center gap-1 bg-surface-100 dark:bg-surface-800 rounded-lg p-1">
          {[
            { id: 'overview', name: 'Overview' },
            { id: 'architecture', name: 'Architecture' },
            { id: 'performance', name: 'Performance' },
            { id: 'requirements', name: 'Requirements' },
          ].map(tab => (
            <button
              key={tab.id}
              onClick={() => setActiveSpecTab(tab.id as any)}
              className={clsx(
                'flex-1 px-3 py-1.5 rounded-md text-sm font-medium transition-colors',
                activeSpecTab === tab.id
                  ? 'bg-white dark:bg-surface-700 text-surface-900 dark:text-white shadow-sm'
                  : 'text-surface-500 hover:text-surface-700 dark:hover:text-surface-300'
              )}
            >
              {tab.name}
            </button>
          ))}
        </div>
      </div>

      {/* Content */}
      <div className="p-4 space-y-4">
        {/* Overview Tab */}
        {activeSpecTab === 'overview' && (
          <>
            {/* Status Badge */}
            <div className="flex items-center gap-2">
              <Badge variant="success" size="sm">
                <span className="w-1.5 h-1.5 rounded-full bg-current mr-1.5 animate-pulse" />
                {model.status}
              </Badge>
              <Badge variant="brand" size="sm">{model.device}</Badge>
              <Badge variant="default" size="sm">{specs.quantization}</Badge>
            </div>

            {/* Key Specs Grid */}
            <div className="grid grid-cols-2 gap-3">
              <div className="p-3 rounded-xl bg-gradient-to-br from-brand-50 to-brand-100/50 dark:from-brand-900/20 dark:to-brand-900/10 border border-brand-200 dark:border-brand-800">
                <p className="text-xs text-brand-600 dark:text-brand-400 mb-1">Parameters</p>
                <p className="text-lg font-bold text-brand-700 dark:text-brand-300">{specs.parameters}</p>
              </div>
              <div className="p-3 rounded-xl bg-gradient-to-br from-ai-purple/10 to-ai-purple/5 dark:from-ai-purple/20 dark:to-ai-purple/10 border border-ai-purple/20 dark:border-ai-purple/30">
                <p className="text-xs text-ai-purple mb-1">Context Length</p>
                <p className="text-lg font-bold text-ai-purple">{specs.contextLength.toLocaleString()} tokens</p>
              </div>
              <div className="p-3 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <p className="text-xs text-surface-500 mb-1">File Size</p>
                <p className="text-lg font-bold text-surface-900 dark:text-white">{specs.fileSize}</p>
              </div>
              <div className="p-3 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <p className="text-xs text-surface-500 mb-1">Format</p>
                <p className="text-lg font-bold text-surface-900 dark:text-white">{specs.format}</p>
              </div>
            </div>

            {/* Capabilities */}
            <div>
              <h3 className="text-sm font-semibold text-surface-900 dark:text-white mb-2">Capabilities</h3>
              <div className="flex flex-wrap gap-2">
                {specs.capabilities.map(cap => (
                  <Badge key={cap} variant="default" size="sm">
                    <CheckCircle2 className="w-3 h-3 mr-1 text-success-500" />
                    {cap}
                  </Badge>
                ))}
              </div>
            </div>

            {/* Real-time Stats */}
            <div>
              <h3 className="text-sm font-semibold text-surface-900 dark:text-white mb-2">Live Statistics</h3>
              <div className="space-y-2">
                <div className="flex items-center justify-between p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <span className="text-sm text-surface-600 dark:text-surface-400">Memory Usage</span>
                  <span className="text-sm font-semibold text-surface-900 dark:text-white">
                    {((model.ram_usage_mb || 0) / 1024).toFixed(2)} GB
                  </span>
                </div>
                <div className="flex items-center justify-between p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <span className="text-sm text-surface-600 dark:text-surface-400">Current Throughput</span>
                  <span className="text-sm font-semibold text-surface-900 dark:text-white">
                    {(model.throughput_toks || 0).toFixed(1)} tok/s
                  </span>
                </div>
                <div className="flex items-center justify-between p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <span className="text-sm text-surface-600 dark:text-surface-400">Average Latency</span>
                  <span className="text-sm font-semibold text-surface-900 dark:text-white">
                    {(model.avg_latency_ms || 0).toFixed(0)} ms
                  </span>
                </div>
                <div className="flex items-center justify-between p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <span className="text-sm text-surface-600 dark:text-surface-400">Requests/Hour</span>
                  <span className="text-sm font-semibold text-surface-900 dark:text-white">
                    {model.requests_per_hour}
                  </span>
                </div>
              </div>
            </div>
          </>
        )}

        {/* Architecture Tab */}
        {activeSpecTab === 'architecture' && (
          <>
            <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
              <h3 className="text-sm font-semibold text-surface-900 dark:text-white mb-3">Model Architecture</h3>
              <code className="text-xs text-brand-600 dark:text-brand-400 bg-brand-50 dark:bg-brand-900/30 px-2 py-1 rounded">
                {specs.architecture}
              </code>
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <div className="flex items-center gap-2 mb-2">
                  <Layers className="w-4 h-4 text-brand-500" />
                  <span className="text-xs text-surface-500">Layers</span>
                </div>
                <p className="text-xl font-bold text-surface-900 dark:text-white">{specs.numLayers}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <div className="flex items-center gap-2 mb-2">
                  <Network className="w-4 h-4 text-ai-purple" />
                  <span className="text-xs text-surface-500">Attention Heads</span>
                </div>
                <p className="text-xl font-bold text-surface-900 dark:text-white">{specs.numHeads}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <div className="flex items-center gap-2 mb-2">
                  <Box className="w-4 h-4 text-success-500" />
                  <span className="text-xs text-surface-500">Hidden Size</span>
                </div>
                <p className="text-xl font-bold text-surface-900 dark:text-white">{specs.hiddenSize.toLocaleString()}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <div className="flex items-center gap-2 mb-2">
                  <FileText className="w-4 h-4 text-warning-500" />
                  <span className="text-xs text-surface-500">Vocabulary</span>
                </div>
                <p className="text-xl font-bold text-surface-900 dark:text-white">{specs.vocabSize.toLocaleString()}</p>
              </div>
            </div>

            {/* Visual Architecture Flow */}
            <div className="p-4 rounded-xl bg-gradient-to-br from-surface-100 to-surface-50 dark:from-surface-800 dark:to-surface-900 border border-surface-200 dark:border-surface-700">
              <h4 className="text-xs font-medium text-surface-500 mb-3">Data Flow</h4>
              <div className="flex items-center justify-between text-xs">
                <div className="text-center p-2 rounded bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                  <Binary className="w-4 h-4 mx-auto mb-1 text-brand-500" />
                  <span>Input</span>
                </div>
                <ChevronRight className="w-4 h-4 text-surface-400" />
                <div className="text-center p-2 rounded bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                  <Box className="w-4 h-4 mx-auto mb-1 text-ai-purple" />
                  <span>Embed</span>
                </div>
                <ChevronRight className="w-4 h-4 text-surface-400" />
                <div className="text-center p-2 rounded bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                  <Layers className="w-4 h-4 mx-auto mb-1 text-success-500" />
                  <span>{specs.numLayers}x</span>
                </div>
                <ChevronRight className="w-4 h-4 text-surface-400" />
                <div className="text-center p-2 rounded bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                  <Activity className="w-4 h-4 mx-auto mb-1 text-warning-500" />
                  <span>Norm</span>
                </div>
                <ChevronRight className="w-4 h-4 text-surface-400" />
                <div className="text-center p-2 rounded bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                  <Zap className="w-4 h-4 mx-auto mb-1 text-error-500" />
                  <span>Output</span>
                </div>
              </div>
            </div>
          </>
        )}

        {/* Performance Tab */}
        {activeSpecTab === 'performance' && (
          <>
            <div className="space-y-3">
              <div className="p-4 rounded-xl bg-gradient-to-br from-success-50 to-success-100/50 dark:from-success-900/20 dark:to-success-900/10 border border-success-200 dark:border-success-800">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-sm text-success-700 dark:text-success-300">Prompt Processing</span>
                  <Zap className="w-4 h-4 text-success-500" />
                </div>
                <p className="text-2xl font-bold text-success-800 dark:text-success-200">{specs.performance.promptSpeed}</p>
                <p className="text-xs text-success-600 dark:text-success-400 mt-1">Input tokenization speed</p>
              </div>

              <div className="p-4 rounded-xl bg-gradient-to-br from-brand-50 to-brand-100/50 dark:from-brand-900/20 dark:to-brand-900/10 border border-brand-200 dark:border-brand-800">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-sm text-brand-700 dark:text-brand-300">Generation Speed</span>
                  <TrendingUp className="w-4 h-4 text-brand-500" />
                </div>
                <p className="text-2xl font-bold text-brand-800 dark:text-brand-200">{specs.performance.genSpeed}</p>
                <p className="text-xs text-brand-600 dark:text-brand-400 mt-1">Token generation rate</p>
              </div>

              <div className="p-4 rounded-xl bg-gradient-to-br from-ai-purple/10 to-ai-purple/5 dark:from-ai-purple/20 dark:to-ai-purple/10 border border-ai-purple/20 dark:border-ai-purple/30">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-sm text-ai-purple">Time to First Token</span>
                  <Clock className="w-4 h-4 text-ai-purple" />
                </div>
                <p className="text-2xl font-bold text-ai-purple">{specs.performance.firstToken}</p>
                <p className="text-xs text-ai-purple/70 mt-1">Initial response latency</p>
              </div>
            </div>

            {/* Performance Comparison */}
            <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800">
              <h4 className="text-sm font-semibold text-surface-900 dark:text-white mb-3">Quantization Impact</h4>
              <div className="space-y-2">
                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-surface-500">Q4_K_M (4-bit)</span>
                    <span className="text-surface-700 dark:text-surface-300">Fastest, Good Quality</span>
                  </div>
                  <Progress value={95} variant="success" />
                </div>
                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-surface-500">Q5_K_M (5-bit)</span>
                    <span className="text-surface-700 dark:text-surface-300">Balanced</span>
                  </div>
                  <Progress value={80} variant="brand" />
                </div>
                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-surface-500">Q8_0 (8-bit)</span>
                    <span className="text-surface-700 dark:text-surface-300">Best Quality, Slower</span>
                  </div>
                  <Progress value={60} variant="warning" />
                </div>
              </div>
            </div>
          </>
        )}

        {/* Requirements Tab */}
        {activeSpecTab === 'requirements' && (
          <>
            <div className="grid grid-cols-1 gap-3">
              <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <div className="flex items-center gap-2 mb-3">
                  <MemoryStick className="w-5 h-5 text-brand-500" />
                  <h4 className="font-semibold text-surface-900 dark:text-white">System RAM</h4>
                </div>
                <div className="space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-sm text-surface-500">Minimum</span>
                    <Badge variant="warning" size="sm">{specs.memoryRequirements.minimum}</Badge>
                  </div>
                  <div className="flex justify-between items-center">
                    <span className="text-sm text-surface-500">Recommended</span>
                    <Badge variant="success" size="sm">{specs.memoryRequirements.recommended}</Badge>
                  </div>
                </div>
              </div>

              <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <div className="flex items-center gap-2 mb-3">
                  <Zap className="w-5 h-5 text-ai-purple" />
                  <h4 className="font-semibold text-surface-900 dark:text-white">GPU VRAM</h4>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-surface-500">Required for full offload</span>
                  <Badge variant="brand" size="sm">{specs.memoryRequirements.vram}</Badge>
                </div>
              </div>

              <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <div className="flex items-center gap-2 mb-3">
                  <HardDrive className="w-5 h-5 text-success-500" />
                  <h4 className="font-semibold text-surface-900 dark:text-white">Storage</h4>
                </div>
                <div className="flex justify-between items-center">
                  <span className="text-sm text-surface-500">Model file size</span>
                  <Badge variant="default" size="sm">{specs.fileSize}</Badge>
                </div>
              </div>
            </div>

            {/* Compatibility */}
            <div className="p-4 rounded-xl bg-gradient-to-br from-brand-50 to-ai-purple/10 dark:from-brand-900/20 dark:to-ai-purple/20 border border-brand-200 dark:border-brand-800">
              <h4 className="font-semibold text-surface-900 dark:text-white mb-3">Supported Backends</h4>
              <div className="grid grid-cols-2 gap-2">
                {RUNTIME_BACKENDS.map(backend => (
                  <div key={backend.id} className="flex items-center gap-2 p-2 rounded-lg bg-white/50 dark:bg-surface-800/50">
                    <CheckCircle2 className="w-4 h-4 text-success-500" />
                    <span className="text-sm text-surface-700 dark:text-surface-300">{backend.name}</span>
                  </div>
                ))}
              </div>
            </div>

            {/* License Info */}
            <div className="p-4 rounded-xl border border-surface-200 dark:border-surface-700">
              <div className="flex items-start gap-3">
                <Info className="w-5 h-5 text-brand-500 flex-shrink-0 mt-0.5" />
                <div>
                  <h4 className="font-semibold text-surface-900 dark:text-white mb-1">License</h4>
                  <p className="text-sm text-surface-600 dark:text-surface-400">{specs.license}</p>
                  <p className="text-xs text-surface-500 mt-2">
                    Created by {specs.creator}. Please review the license terms before commercial use.
                  </p>
                </div>
              </div>
            </div>
          </>
        )}
      </div>
    </motion.div>
  );
}

// ============================================================================
// Model Architecture Graph Component
// ============================================================================

function ModelArchitectureGraph({ model }: { model: LoadedModel }) {
  const [expanded, setExpanded] = useState(false);

  // Simulated architecture data based on model name
  const getArchitecture = () => {
    const name = model.name.toLowerCase();
    if (name.includes('llama')) {
      return {
        type: 'Transformer (Decoder-Only)',
        layers: 32,
        hiddenSize: 4096,
        heads: 32,
        vocab: 128256,
        architecture: 'LlamaForCausalLM',
        nodes: [
          { id: 'embed', name: 'Embedding', size: '128K × 4096' },
          { id: 'layers', name: 'Transformer Layers', size: '32 layers' },
          { id: 'norm', name: 'RMSNorm', size: '4096' },
          { id: 'lm_head', name: 'LM Head', size: '4096 × 128K' },
        ],
      };
    }
    if (name.includes('mistral')) {
      return {
        type: 'Transformer (SWA)',
        layers: 32,
        hiddenSize: 4096,
        heads: 32,
        vocab: 32000,
        architecture: 'MistralForCausalLM',
        nodes: [
          { id: 'embed', name: 'Embedding', size: '32K × 4096' },
          { id: 'layers', name: 'SWA Layers', size: '32 layers' },
          { id: 'norm', name: 'RMSNorm', size: '4096' },
          { id: 'lm_head', name: 'LM Head', size: '4096 × 32K' },
        ],
      };
    }
    return {
      type: 'Transformer',
      layers: 24,
      hiddenSize: 2048,
      heads: 16,
      vocab: 50000,
      architecture: 'AutoModelForCausalLM',
      nodes: [
        { id: 'embed', name: 'Embedding', size: '50K × 2048' },
        { id: 'layers', name: 'Transformer Layers', size: '24 layers' },
        { id: 'norm', name: 'LayerNorm', size: '2048' },
        { id: 'lm_head', name: 'LM Head', size: '2048 × 50K' },
      ],
    };
  };

  const arch = getArchitecture();

  return (
    <div className="space-y-4">
      <button
        onClick={() => setExpanded(!expanded)}
        className="flex items-center gap-2 text-sm font-medium text-brand-600 dark:text-brand-400 hover:underline"
      >
        <GitBranch className="w-4 h-4" />
        View Architecture
        {expanded ? <ChevronDown className="w-4 h-4" /> : <ChevronRight className="w-4 h-4" />}
      </button>

      <AnimatePresence>
        {expanded && (
          <motion.div
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            className="space-y-4"
          >
            {/* Architecture Info */}
            <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
              <div className="p-3 rounded-lg bg-surface-100 dark:bg-surface-800">
                <p className="text-xs text-surface-500 mb-1">Architecture</p>
                <p className="text-sm font-medium text-surface-900 dark:text-white">{arch.type}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-100 dark:bg-surface-800">
                <p className="text-xs text-surface-500 mb-1">Layers</p>
                <p className="text-sm font-medium text-surface-900 dark:text-white">{arch.layers}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-100 dark:bg-surface-800">
                <p className="text-xs text-surface-500 mb-1">Hidden Size</p>
                <p className="text-sm font-medium text-surface-900 dark:text-white">{arch.hiddenSize}</p>
              </div>
              <div className="p-3 rounded-lg bg-surface-100 dark:bg-surface-800">
                <p className="text-xs text-surface-500 mb-1">Attention Heads</p>
                <p className="text-sm font-medium text-surface-900 dark:text-white">{arch.heads}</p>
              </div>
            </div>

            {/* Visual Graph */}
            <div className="relative p-4 rounded-xl bg-gradient-to-br from-surface-100 to-surface-50 dark:from-surface-800 dark:to-surface-900 border border-surface-200 dark:border-surface-700">
              <div className="flex items-center justify-between gap-2">
                {arch.nodes.map((node, i) => (
                  <React.Fragment key={node.id}>
                    <div className="flex-1 p-3 rounded-lg bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 text-center">
                      <div className="w-8 h-8 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center mx-auto mb-2">
                        <Box className="w-4 h-4 text-brand-600 dark:text-brand-400" />
                      </div>
                      <p className="text-xs font-medium text-surface-900 dark:text-white">{node.name}</p>
                      <p className="text-xs text-surface-500">{node.size}</p>
                    </div>
                    {i < arch.nodes.length - 1 && (
                      <ChevronRight className="w-5 h-5 text-surface-400 flex-shrink-0" />
                    )}
                  </React.Fragment>
                ))}
              </div>
            </div>

            {/* Class Name */}
            <div className="flex items-center gap-2 text-xs text-surface-500">
              <code className="px-2 py-1 rounded bg-surface-100 dark:bg-surface-800 font-mono">
                {arch.architecture}
              </code>
              <span>•</span>
              <span>Vocabulary: {arch.vocab.toLocaleString()} tokens</span>
            </div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}

// ============================================================================
// Runtime Configuration Modal
// ============================================================================

function RuntimeConfigModal({
  isOpen,
  onClose,
  onApply,
}: {
  isOpen: boolean;
  onClose: () => void;
  onApply: (config: RuntimeConfig) => void;
}) {
  const [config, setConfig] = useState<RuntimeConfig>({
    backend: 'cuda',
    gpuLayers: 35,
    threads: 8,
    batchSize: 512,
    contextSize: 4096,
    flashAttention: true,
    mmap: true,
    mlock: false,
  });

  return (
    <Modal isOpen={isOpen} onClose={onClose} title="Runtime Configuration" size="lg">
      <div className="space-y-6">
        {/* Backend Selection */}
        <div>
          <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-3 block">
            Compute Backend
          </label>
          <div className="grid grid-cols-2 gap-3">
            {RUNTIME_BACKENDS.map(backend => (
              <button
                key={backend.id}
                onClick={() => setConfig(c => ({ ...c, backend: backend.id as any }))}
                className={clsx(
                  'p-4 rounded-xl border text-left transition-all',
                  config.backend === backend.id
                    ? 'border-brand-500 bg-brand-50 dark:bg-brand-900/20'
                    : 'border-surface-200 dark:border-surface-700 hover:border-surface-300'
                )}
              >
                <div className="flex items-center gap-3">
                  <div className={clsx(
                    'w-10 h-10 rounded-lg flex items-center justify-center',
                    `bg-${backend.color}-100 dark:bg-${backend.color}-900/30`
                  )}>
                    <backend.icon className={`w-5 h-5 text-${backend.color}-600 dark:text-${backend.color}-400`} />
                  </div>
                  <div>
                    <p className="font-medium text-surface-900 dark:text-white">{backend.name}</p>
                    <p className="text-xs text-surface-500">{backend.description}</p>
                  </div>
                </div>
              </button>
            ))}
          </div>
        </div>

        {/* GPU Layers Slider */}
        {config.backend !== 'cpu' && (
          <div>
            <div className="flex items-center justify-between mb-2">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                GPU Layers
              </label>
              <span className="text-sm font-medium text-brand-600">{config.gpuLayers}</span>
            </div>
            <input
              type="range"
              min="0"
              max="100"
              value={config.gpuLayers}
              onChange={(e) => setConfig(c => ({ ...c, gpuLayers: parseInt(e.target.value) }))}
              className="w-full accent-brand-500"
            />
            <div className="flex justify-between text-xs text-surface-500 mt-1">
              <span>CPU Only</span>
              <span>All on GPU</span>
            </div>
          </div>
        )}

        {/* Thread Count */}
        <div>
          <div className="flex items-center justify-between mb-2">
            <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
              CPU Threads
            </label>
            <span className="text-sm font-medium text-brand-600">{config.threads}</span>
          </div>
          <input
            type="range"
            min="1"
            max="32"
            value={config.threads}
            onChange={(e) => setConfig(c => ({ ...c, threads: parseInt(e.target.value) }))}
            className="w-full accent-brand-500"
          />
        </div>

        {/* Context Size */}
        <div>
          <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
            Context Size
          </label>
          <select
            value={config.contextSize}
            onChange={(e) => setConfig(c => ({ ...c, contextSize: parseInt(e.target.value) }))}
            className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800"
          >
            <option value="2048">2,048 tokens</option>
            <option value="4096">4,096 tokens</option>
            <option value="8192">8,192 tokens</option>
            <option value="16384">16,384 tokens</option>
            <option value="32768">32,768 tokens</option>
            <option value="131072">131,072 tokens (128K)</option>
          </select>
        </div>

        {/* Advanced Options */}
        <div className="space-y-3">
          <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
            Advanced Options
          </label>

          <div className="flex items-center justify-between p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">Flash Attention</p>
              <p className="text-xs text-surface-500">Faster attention computation (requires compatible GPU)</p>
            </div>
            <Toggle
              checked={config.flashAttention}
              onChange={(v) => setConfig(c => ({ ...c, flashAttention: v }))}
            />
          </div>

          <div className="flex items-center justify-between p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">Memory Mapping (mmap)</p>
              <p className="text-xs text-surface-500">Map model file directly into memory</p>
            </div>
            <Toggle
              checked={config.mmap}
              onChange={(v) => setConfig(c => ({ ...c, mmap: v }))}
            />
          </div>

          <div className="flex items-center justify-between p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">Memory Lock (mlock)</p>
              <p className="text-xs text-surface-500">Prevent model from being swapped to disk</p>
            </div>
            <Toggle
              checked={config.mlock}
              onChange={(v) => setConfig(c => ({ ...c, mlock: v }))}
            />
          </div>
        </div>

        {/* Actions */}
        <div className="flex justify-end gap-2 pt-4 border-t border-surface-200 dark:border-surface-700">
          <Button variant="secondary" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" onClick={() => { onApply(config); onClose(); }}>
            <Settings2 className="w-4 h-4" />
            Apply Configuration
          </Button>
        </div>
      </div>
    </Modal>
  );
}

// ============================================================================
// Main Component
// ============================================================================

export default function Models() {
  const queryClient = useQueryClient();
  const [activeTab, setActiveTab] = useState<'loaded' | 'available'>('loaded');
  const [searchQuery, setSearchQuery] = useState('');
  const [showLoadForm, setShowLoadForm] = useState(false);
  const [showRuntimeConfig, setShowRuntimeConfig] = useState(false);
  const [modelId, setModelId] = useState('');
  const [filePath, setFilePath] = useState('');
  const [strategy, setStrategy] = useState('balanced');
  const [modelType, setModelType] = useState<'auto' | 'llm' | 'diffusion' | 'vision'>('auto');
  const [filterTab, setFilterTab] = useState<'all' | 'llm' | 'diffusion' | 'vision'>('all');
  // Multi-file model paths (for SD3, FLUX, Wan2, Vision)
  const [vaePath, setVaePath] = useState('');
  const [t5xxlPath, setT5xxlPath] = useState('');
  const [clipLPath, setClipLPath] = useState('');
  const [clipGPath, setClipGPath] = useState('');
  const [mmprojPath, setMmprojPath] = useState('');
  const [notification, setNotification] = useState<{ type: 'success' | 'error'; message: string } | null>(null);
  const [selectedModel, setSelectedModel] = useState<LoadedModel | null>(null);
  const [modelsFolder, setModelsFolder] = useState<string>(getDefaultModelsPath());
  const [folderModels, setFolderModels] = useState<string[]>([]);
  const [isScanning, setIsScanning] = useState(false);
  const [scanError, setScanError] = useState<string | null>(null);
  const [showWorkspaceManager, setShowWorkspaceManager] = useState(false);
  const [workspaceFolders, setWorkspaceFolders] = useState<WorkspaceFolderInfo[]>([]);
  const [isLoadingWorkspace, setIsLoadingWorkspace] = useState(false);
  const [deletingFolder, setDeletingFolder] = useState<string | null>(null);
  const [confirmDeleteFolder, setConfirmDeleteFolder] = useState<WorkspaceFolderInfo | null>(null);

  // Sync defaults from server config (if available)
  useEffect(() => {
    const loadConfigDefaults = async () => {
      try {
        const cfg = await getConfig();
        if (cfg?.default_models_path) {
          setModelsFolder(cfg.default_models_path.replace(/\\/g, '/'));
        }
      } catch {
        // Ignore and keep local defaults
      }
    };
    loadConfigDefaults();
  }, []);

  // Scan the models folder for .gguf files
  const scanCurrentFolder = async () => {
    setIsScanning(true);
    setScanError(null);
    try {
      console.log('[Models] Scanning folder:', modelsFolder);
      const models = await scanFolder(modelsFolder);
      console.log('[Models] Scan result:', models);
      setFolderModels(models);
      if (models.length === 0) {
        setScanError('No GGUF models found. Make sure the SnapLLM server is running on port 6930.');
      }
    } catch (error: any) {
      console.error('[Models] Error scanning folder:', error);
      setFolderModels([]);
      setScanError(`Scan failed: ${error?.message || 'Server may be offline'}`);
    } finally {
      setIsScanning(false);
    }
  };

  // Handle selecting a different models folder
  const handleBrowseFolder = async () => {
    const folder = await selectModelsFolder();
    if (folder) {
      setModelsFolder(folder);
      // Scan the new folder
      setIsScanning(true);
      try {
        const models = await scanFolder(folder);
        setFolderModels(models);
      } catch (error) {
        console.error('[Models] Error scanning folder:', error);
        setFolderModels([]);
      } finally {
        setIsScanning(false);
      }
    }
  };

  // Handle direct file selection
  const handleBrowseFile = async () => {
    const file = await selectModelFile();
    if (file) {
      handleFilePathChange(file);
    }
  };

  // Scan folder when modal opens
  useEffect(() => {
    if (showLoadForm) {
      scanCurrentFolder();
    }
  }, [showLoadForm, modelsFolder]);

  // Load workspace folders info
  const loadWorkspaceFolders = async () => {
    setIsLoadingWorkspace(true);
    try {
      const folders = await getWorkspaceFolders();
      setWorkspaceFolders(folders);
    } catch (error) {
      console.error('[Models] Error loading workspace folders:', error);
      setWorkspaceFolders([]);
    } finally {
      setIsLoadingWorkspace(false);
    }
  };

  // Load workspace folders when modal opens
  useEffect(() => {
    if (showWorkspaceManager) {
      loadWorkspaceFolders();
    }
  }, [showWorkspaceManager]);

  // Handle folder deletion
  const handleDeleteFolder = async (folder: WorkspaceFolderInfo) => {
    setDeletingFolder(folder.path);
    try {
      await deleteWorkspaceFolder(folder.path);
      setNotification({ type: 'success', message: `Successfully deleted ${folder.name}` });
      setTimeout(() => setNotification(null), 5000);
      // Refresh the workspace folders list
      await loadWorkspaceFolders();
    } catch (error) {
      console.error('[Models] Error deleting folder:', error);
      setNotification({ type: 'error', message: `Failed to delete ${folder.name}: ${error}` });
      setTimeout(() => setNotification(null), 8000);
    } finally {
      setDeletingFolder(null);
      setConfirmDeleteFolder(null);
    }
  };

  // Queries
  const { data: modelsResponse, isLoading } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
  });

  const { data: availableModels, isLoading: isLoadingAvailable } = useQuery({
    queryKey: ['available-models'],
    queryFn: scanModelsFolder,
    refetchInterval: 10000,
  });

  const { data: cacheStats } = useQuery({
    queryKey: ['cache-stats'],
    queryFn: getCacheStats,
    refetchInterval: 10000,
  });

  const models = modelsResponse?.models || [];

  // Mutations
  const loadMutation = useMutation({
    mutationFn: loadModel,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['models'] });
      setNotification({ type: 'success', message: `Model "${modelId}" loaded successfully!` });
      setTimeout(() => setNotification(null), 5000);
      setShowLoadForm(false);
      setModelId('');
      setFilePath('');
    },
    onError: (error: any) => {
      setNotification({ type: 'error', message: `Failed to load model: ${error.message}` });
      setTimeout(() => setNotification(null), 8000);
    },
  });

  const unloadMutation = useMutation({
    mutationFn: unloadModel,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['models'] });
      setNotification({ type: 'success', message: 'Model unloaded successfully!' });
      setTimeout(() => setNotification(null), 5000);
    },
    onError: (error: any) => {
      setNotification({ type: 'error', message: `Failed to unload model: ${error.message || 'Unknown error'}` });
      setTimeout(() => setNotification(null), 8000);
    },
  });

  const handleFilePathChange = (path: string) => {
    setFilePath(path);
    if (path) {
      const filename = path.split('/').pop()?.replace('.gguf', '').replace('.safetensors', '') || '';
      const cleanId = filename.toLowerCase().replace(/[^a-z0-9]/g, '-');
      setModelId(cleanId);

      // Auto-detect model type based on filename
      const detectedType = detectModelType(path);
      if (detectedType !== 'unknown') {
        setModelType(detectedType as 'llm' | 'diffusion' | 'vision');
      }
    }
  };

  const totalMemory = models.reduce((sum, m) => sum + (m.ram_usage_mb || 0), 0);
  const totalThroughput = models.reduce((sum, m) => sum + (m.throughput_toks || 0), 0);

  return (
    <div className="space-y-6">
      {/* Notification Toast */}
      <AnimatePresence>
        {notification && (
          <motion.div
            initial={{ opacity: 0, y: -20, x: '-50%' }}
            animate={{ opacity: 1, y: 0, x: '-50%' }}
            exit={{ opacity: 0, y: -20 }}
            className={clsx(
              'fixed top-4 left-1/2 z-50 px-4 py-3 rounded-xl shadow-lg flex items-center gap-3',
              notification.type === 'success'
                ? 'bg-success-50 dark:bg-success-900/90 text-success-800 dark:text-success-200 border border-success-200 dark:border-success-800'
                : 'bg-error-50 dark:bg-error-900/90 text-error-800 dark:text-error-200 border border-error-200 dark:border-error-800'
            )}
          >
            {notification.type === 'success' ? (
              <CheckCircle2 className="w-5 h-5" />
            ) : (
              <XCircle className="w-5 h-5" />
            )}
            <span className="font-medium">{notification.message}</span>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Model Hub
          </h1>
          <p className="text-surface-500 mt-1">
            Manage, download, and configure your AI models
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="secondary" onClick={() => setShowWorkspaceManager(true)}>
            <HardDrive className="w-4 h-4" />
            Workspace
          </Button>
          <Button variant="secondary" onClick={() => setShowRuntimeConfig(true)}>
            <Settings2 className="w-4 h-4" />
            Runtime Config
          </Button>
          <Button
            variant="primary"
            onClick={() => setShowLoadForm(true)}
            className="bg-gradient-to-r from-sky-500 to-sky-600 hover:from-sky-600 hover:to-sky-700 shadow-lg shadow-sky-500/25"
          >
            <Plus className="w-4 h-4" />
            Load Model
          </Button>
        </div>
      </div>

      {/* Stats Overview */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Cpu className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">{models.length}</p>
              <p className="text-sm text-surface-500">Models Loaded</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <MemoryStick className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {(totalMemory / 1024).toFixed(1)} GB
              </p>
              <p className="text-sm text-surface-500">Memory Used</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
              <Zap className="w-5 h-5 text-ai-purple" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {totalThroughput.toFixed(0)}
              </p>
              <p className="text-sm text-surface-500">tok/s Total</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Database className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {cacheStats?.total_memory_mb?.toFixed(0) || 0} MB
              </p>
              <p className="text-sm text-surface-500">Cache Size</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Tab Navigation */}
      <div className="flex items-center gap-2 border-b border-surface-200 dark:border-surface-700">
        {[
          { id: 'loaded', name: 'Loaded Models', icon: Cpu, count: models.length },
          { id: 'available', name: 'Local Models', icon: FolderOpen, count: availableModels?.length || 0 },
        ].map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id as any)}
            className={clsx(
              'flex items-center gap-2 px-4 py-3 text-sm font-medium border-b-2 transition-colors',
              activeTab === tab.id
                ? 'border-brand-500 text-brand-600 dark:text-brand-400'
                : 'border-transparent text-surface-500 hover:text-surface-700 dark:hover:text-surface-300'
            )}
          >
            <tab.icon className="w-4 h-4" />
            {tab.name}
            {tab.count !== undefined && (
              <Badge variant="default" size="sm">{tab.count}</Badge>
            )}
          </button>
        ))}
      </div>

      {/* Loaded Models Tab */}
      {activeTab === 'loaded' && (
        <div className="space-y-4">
          {isLoading ? (
            <Card className="p-12 text-center">
              <Loader className="w-8 h-8 animate-spin mx-auto text-brand-500 mb-3" />
              <p className="text-surface-500">Loading models...</p>
            </Card>
          ) : models.length > 0 ? (
            models.map((model, i) => {
              // Determine model type from server response or detect from name
              const modelType = (model as any).type || detectModelType(model.name);
              const TypeIcon = modelType === 'diffusion' ? Sparkles
                : modelType === 'vision' ? Eye
                : FileText;

              return (
              <motion.div
                key={model.id}
                initial={{ opacity: 0, y: 20 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: i * 0.1 }}
              >
                <Card
                    className="p-6 cursor-pointer hover:border-brand-300 dark:hover:border-brand-700 transition-all"
                    onClick={() => setSelectedModel(model)}
                  >
                  <div className="flex items-start justify-between mb-4">
                    <div className="flex items-center gap-4">
                      <div className={clsx(
                        'w-12 h-12 rounded-xl flex items-center justify-center',
                        modelType === 'diffusion' ? 'bg-gradient-to-br from-success-500 to-emerald-600'
                          : modelType === 'vision' ? 'bg-gradient-to-br from-warning-500 to-orange-600'
                          : 'bg-gradient-to-br from-brand-500 to-ai-purple'
                      )}>
                        <TypeIcon className="w-6 h-6 text-white" />
                      </div>
                      <div>
                        <div className="flex items-center gap-2 mb-1">
                          <h3 className="text-lg font-semibold text-surface-900 dark:text-white">
                            {model.name}
                          </h3>
                          <Badge variant="success">
                            <span className="w-1.5 h-1.5 rounded-full bg-current mr-1.5" />
                            {model.status}
                          </Badge>
                          <Badge variant={getModelTypeBadgeColor(modelType as ModelType)} size="sm">
                            {getModelTypeLabel(modelType as ModelType)}
                          </Badge>
                        </div>
                        <div className="flex items-center gap-4 text-sm text-surface-500">
                          <span className="flex items-center gap-1">
                            <CircuitBoard className="w-3 h-3" />
                            {model.engine || (modelType === 'diffusion' ? 'stable-diffusion' : 'llama.cpp')}
                          </span>
                          <span className="flex items-center gap-1">
                            {model.device === 'CUDA' ? <Zap className="w-3 h-3" /> : <Cpu className="w-3 h-3" />}
                            {model.device}
                          </span>
                          {model.strategy && (
                            <span className="flex items-center gap-1">
                              <Settings2 className="w-3 h-3" />
                              {model.strategy}
                            </span>
                          )}
                        </div>
                      </div>
                    </div>

                    <div className="flex items-center gap-2" onClick={(e) => e.stopPropagation()}>
                      <Button variant="secondary" size="sm" onClick={() => setSelectedModel(model)}>
                        <Eye className="w-4 h-4" />
                        Details
                      </Button>
                      <IconButton icon={<RefreshCw className="w-4 h-4" />} label="Reload" />
                      <IconButton icon={<Settings2 className="w-4 h-4" />} label="Configure" />
                      <IconButton
                        icon={<Trash2 className="w-4 h-4 text-error-500" />}
                        label="Unload"
                        onClick={() => unloadMutation.mutate(model.id)}
                      />
                    </div>
                  </div>

                  {/* Performance Stats */}
                  <div className="grid grid-cols-4 gap-4 mb-4">
                    <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                      <p className="text-xs text-surface-500 mb-1">Memory</p>
                      <p className="text-lg font-semibold text-surface-900 dark:text-white">
                        {((model.ram_usage_mb || 0) / 1024).toFixed(2)} GB
                      </p>
                    </div>
                    <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                      <p className="text-xs text-surface-500 mb-1">Throughput</p>
                      <p className="text-lg font-semibold text-surface-900 dark:text-white">
                        {(model.throughput_toks || 0).toFixed(1)} tok/s
                      </p>
                    </div>
                    <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                      <p className="text-xs text-surface-500 mb-1">Latency</p>
                      <p className="text-lg font-semibold text-surface-900 dark:text-white">
                        {(model.avg_latency_ms || 0).toFixed(0)} ms
                      </p>
                    </div>
                    <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                      <p className="text-xs text-surface-500 mb-1">Requests</p>
                      <p className="text-lg font-semibold text-surface-900 dark:text-white">
                        {model.requests_per_hour}/hr
                      </p>
                    </div>
                  </div>

                  {/* Model Architecture */}
                  <ModelArchitectureGraph model={model} />
                </Card>
              </motion.div>
              );
            })
          ) : (
            <Card className="p-12 text-center">
              <div className="w-16 h-16 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-4">
                <Cpu className="w-8 h-8 text-surface-400" />
              </div>
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
                No Models Loaded
              </h3>
              <p className="text-surface-500 mb-4">
                Load a model to start generating AI responses
              </p>
              <Button
                variant="primary"
                onClick={() => setShowLoadForm(true)}
                className="bg-gradient-to-r from-sky-500 to-sky-600 hover:from-sky-600 hover:to-sky-700 shadow-lg shadow-sky-500/25"
              >
                <Plus className="w-4 h-4" />
                Load Your First Model
              </Button>
            </Card>
          )}
        </div>
      )}

      {/* Available Models Tab */}
      {activeTab === 'available' && (
        <div className="space-y-4">
          <Card className="p-4">
            <div className="flex items-center gap-4">
              <div className="flex-1 relative">
                <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
                <input
                  type="text"
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="Search local models..."
                  className="w-full pl-10 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800"
                />
              </div>
              <Button variant="secondary" onClick={() => queryClient.invalidateQueries({ queryKey: ['available-models'] })}>
                <RefreshCw className="w-4 h-4" />
                Refresh
              </Button>
            </div>
          </Card>

          {isLoadingAvailable ? (
            <Card className="p-12 text-center">
              <Loader className="w-8 h-8 animate-spin mx-auto text-brand-500 mb-3" />
              <p className="text-surface-500">Scanning models folder...</p>
            </Card>
          ) : availableModels && availableModels.length > 0 ? (
            <>
              {/* Group models by detected type */}
              {(['llm', 'diffusion', 'vision'] as const).map(typeGroup => {
                const modelsOfType = availableModels
                  .filter(m => m.toLowerCase().includes(searchQuery.toLowerCase()))
                  .filter(m => detectModelType(m) === typeGroup);

                if (modelsOfType.length === 0) return null;

                const TypeIcon = typeGroup === 'llm' ? FileText
                  : typeGroup === 'diffusion' ? Sparkles
                  : Eye;

                return (
                  <div key={typeGroup} className="mb-6">
                    <div className="flex items-center gap-2 mb-3">
                      <TypeIcon className="w-5 h-5 text-surface-500" />
                      <h3 className="text-sm font-semibold text-surface-700 dark:text-surface-300">
                        {getModelTypeLabel(typeGroup)} ({modelsOfType.length})
                      </h3>
                      <Badge variant={getModelTypeBadgeColor(typeGroup)} size="sm">
                        {typeGroup === 'llm' ? 'llama.cpp' : typeGroup === 'diffusion' ? 'stable-diffusion.cpp' : 'mtmd'}
                      </Badge>
                    </div>
                    <div className="grid grid-cols-2 gap-4">
                      {modelsOfType.map((modelPath, i) => {
                        const fileName = modelPath.split('/').pop() || modelPath;
                        const isLoaded = models.some(m => m.name.toLowerCase().includes(fileName.toLowerCase().replace('.gguf', '').replace('.safetensors', '')));

                        return (
                          <motion.div
                            key={modelPath}
                            initial={{ opacity: 0, y: 20 }}
                            animate={{ opacity: 1, y: 0 }}
                            transition={{ delay: i * 0.05 }}
                          >
                            <Card className={clsx(
                              'p-4 hover:border-brand-300 dark:hover:border-brand-700 transition-colors',
                              isLoaded && 'border-success-300 dark:border-success-700 bg-success-50/50 dark:bg-success-900/10'
                            )}>
                              <div className="flex items-center gap-3">
                                <div className={clsx(
                                  'w-10 h-10 rounded-lg flex items-center justify-center',
                                  isLoaded
                                    ? 'bg-success-100 dark:bg-success-900/30'
                                    : typeGroup === 'llm' ? 'bg-brand-100 dark:bg-brand-900/30'
                                    : typeGroup === 'diffusion' ? 'bg-success-100 dark:bg-success-900/30'
                                    : 'bg-warning-100 dark:bg-warning-900/30'
                                )}>
                                  <TypeIcon className={clsx(
                                    'w-5 h-5',
                                    isLoaded ? 'text-success-600 dark:text-success-400'
                                    : typeGroup === 'llm' ? 'text-brand-600 dark:text-brand-400'
                                    : typeGroup === 'diffusion' ? 'text-success-600 dark:text-success-400'
                                    : 'text-warning-600 dark:text-warning-400'
                                  )} />
                                </div>
                                <div className="flex-1 min-w-0">
                                  <p className="font-medium text-surface-900 dark:text-white truncate">
                                    {fileName}
                                  </p>
                                  <div className="flex items-center gap-2">
                                    <Badge variant={getModelTypeBadgeColor(typeGroup)} size="sm">
                                      {getModelTypeLabel(typeGroup)}
                                    </Badge>
                                  </div>
                                </div>
                                {isLoaded ? (
                                  <Badge variant="success" size="sm">Loaded</Badge>
                                ) : (
                                  <Button
                                    variant="secondary"
                                    size="sm"
                                    onClick={() => {
                                      handleFilePathChange(modelPath);
                                      setShowLoadForm(true);
                                    }}
                                  >
                                    <Play className="w-3 h-3" />
                                    Load
                                  </Button>
                                )}
                              </div>
                            </Card>
                          </motion.div>
                        );
                      })}
                    </div>
                  </div>
                );
              })}
            </>
          ) : (
            <Card className="p-12 text-center">
              <div className="w-16 h-16 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-4">
                <FolderOpen className="w-8 h-8 text-surface-400" />
              </div>
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
                No Local Models Found
              </h3>
              <p className="text-surface-500 mb-4">
                Add GGUF files to your models folder or load a model directly from disk
              </p>
              <Button variant="primary" onClick={() => setShowLoadForm(true)}>
                <Plus className="w-4 h-4" />
                Load Model
              </Button>
            </Card>
          )}
        </div>
      )}

      {/* Load Model Modal - Redesigned with Type Tabs */}
      <AnimatePresence>
        {showLoadForm && (
          <Modal isOpen={showLoadForm} onClose={() => setShowLoadForm(false)} title="Load Model" size="xl">
            <div className="space-y-4">
              {/* Model Type Filter Tabs */}
              <div className="flex items-center gap-1 p-1 bg-surface-100 dark:bg-surface-800 rounded-xl">
                {[
                  { id: 'all', label: 'All Models', icon: Layers },
                  { id: 'llm', label: 'LLM', icon: FileText },
                  { id: 'diffusion', label: 'Image', icon: Sparkles },
                  { id: 'vision', label: 'Vision', icon: Eye },
                ].map(tab => {
                  const TabIcon = tab.icon;
                  const isActive = filterTab === tab.id;
                  const count = tab.id === 'all'
                    ? folderModels.length
                    : folderModels.filter(m => detectModelType(m) === tab.id).length;
                  return (
                    <button
                      key={tab.id}
                      onClick={() => {
                        setFilterTab(tab.id as any);
                        if (tab.id !== 'all') setModelType(tab.id as any);
                      }}
                      className={clsx(
                        'flex-1 flex items-center justify-center gap-2 px-3 py-2 rounded-lg text-sm font-medium transition-all',
                        isActive
                          ? 'bg-white dark:bg-surface-700 text-brand-600 dark:text-brand-400 shadow-sm'
                          : 'text-surface-600 dark:text-surface-400 hover:text-surface-900 dark:hover:text-white'
                      )}
                    >
                      <TabIcon className="w-4 h-4" />
                      <span className="hidden sm:inline">{tab.label}</span>
                      {count > 0 && (
                        <span className={clsx(
                          'text-xs px-1.5 py-0.5 rounded-full',
                          isActive ? 'bg-brand-100 dark:bg-brand-900 text-brand-600 dark:text-brand-400' : 'bg-surface-200 dark:bg-surface-600'
                        )}>
                          {count}
                        </span>
                      )}
                    </button>
                  );
                })}
              </div>

              {/* Folder Section - Compact */}
              <div className="flex items-center gap-2 p-3 rounded-lg bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
                <FolderOpen className="w-4 h-4 text-brand-500 flex-shrink-0" />
                <input
                  type="text"
                  value={modelsFolder}
                  onChange={(e) => setModelsFolder(e.target.value)}
                  placeholder="Models folder path"
                  className="flex-1 px-2 py-1 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                />
                <IconButton
                  icon={<RefreshCw className={clsx("w-4 h-4", isScanning && "animate-spin")} />}
                  label="Scan"
                  size="sm"
                  onClick={scanCurrentFolder}
                />
                {isTauriAvailable() && (
                  <Button variant="secondary" size="sm" onClick={handleBrowseFolder}>
                    Browse
                  </Button>
                )}
              </div>

              {/* Model List - Filtered by Tab */}
              <div className="border border-surface-200 dark:border-surface-700 rounded-xl overflow-hidden">
                {isScanning ? (
                  <div className="flex items-center justify-center p-8">
                    <Loader className="w-5 h-5 animate-spin text-brand-500 mr-2" />
                    <span className="text-sm text-surface-500">Scanning folder...</span>
                  </div>
                ) : scanError ? (
                  <div className="p-4 bg-warning-50 dark:bg-warning-900/20">
                    <div className="flex items-start gap-3">
                      <AlertCircle className="w-5 h-5 text-warning-600 dark:text-warning-400 flex-shrink-0" />
                      <div>
                        <p className="text-sm font-medium text-warning-800 dark:text-warning-200">{scanError}</p>
                        <p className="text-xs text-warning-600 dark:text-warning-400 mt-1">
                          Start server: <code className="px-1 bg-warning-100 dark:bg-warning-800 rounded">snapllm.exe --server --port 6930</code>
                        </p>
                      </div>
                    </div>
                  </div>
                ) : (
                  <div className="max-h-56 overflow-y-auto divide-y divide-surface-100 dark:divide-surface-700">
                    {folderModels
                      .filter(m => filterTab === 'all' || detectModelType(m) === filterTab)
                      .map(model => {
                        const fileName = model.split('/').pop() || model;
                        const isSelected = filePath === model;
                        const detectedType = detectModelType(model);
                        const TypeIcon = detectedType === 'diffusion' ? Sparkles
                          : detectedType === 'vision' ? Eye
                          : FileText;
                        return (
                          <button
                            key={model}
                            onClick={() => handleFilePathChange(model)}
                            className={clsx(
                              'w-full flex items-center gap-3 p-3 transition-all text-left',
                              isSelected
                                ? 'bg-brand-50 dark:bg-brand-900/30'
                                : 'hover:bg-surface-50 dark:hover:bg-surface-800'
                            )}
                          >
                            <div className={clsx(
                              'w-9 h-9 rounded-lg flex items-center justify-center flex-shrink-0',
                              isSelected ? 'bg-brand-500 text-white' : 'bg-surface-100 dark:bg-surface-700 text-surface-500'
                            )}>
                              <TypeIcon className="w-4 h-4" />
                            </div>
                            <div className="flex-1 min-w-0">
                              <p className={clsx(
                                'font-medium truncate text-sm',
                                isSelected ? 'text-brand-700 dark:text-brand-300' : 'text-surface-900 dark:text-white'
                              )}>
                                {fileName}
                              </p>
                              <div className="flex items-center gap-2 mt-0.5">
                                <Badge variant={getModelTypeBadgeColor(detectedType)} size="sm">
                                  {getModelTypeLabel(detectedType)}
                                </Badge>
                              </div>
                            </div>
                            {isSelected && <CheckCircle2 className="w-5 h-5 text-brand-500 flex-shrink-0" />}
                          </button>
                        );
                      })}
                    {folderModels.filter(m => filterTab === 'all' || detectModelType(m) === filterTab).length === 0 && (
                      <div className="text-center p-8">
                        <FolderOpen className="w-10 h-10 mx-auto mb-2 text-surface-300" />
                        <p className="text-sm text-surface-500">
                          {filterTab === 'all' ? 'No models found' : `No ${getModelTypeLabel(filterTab)} models found`}
                        </p>
                      </div>
                    )}
                  </div>
                )}
              </div>

              {/* Direct Path Input */}
              <div className="flex items-center gap-2">
                <input
                  type="text"
                  value={filePath}
                  onChange={(e) => handleFilePathChange(e.target.value)}
                  placeholder="Or enter model path directly..."
                  className="flex-1 px-3 py-2 text-sm rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                />
                {isTauriAvailable() && (
                  <Button variant="secondary" size="sm" onClick={handleBrowseFile}>
                    <Search className="w-4 h-4" />
                  </Button>
                )}
              </div>

              {/* Multi-file Options - Show for Diffusion/Video/Vision */}
              {filePath && (detectModelType(filePath) === 'diffusion' || modelType === 'diffusion') && (
                <div className="p-4 rounded-xl bg-amber-50 dark:bg-amber-900/20 border border-amber-200 dark:border-amber-800">
                  <div className="flex items-center gap-2 mb-3">
                    <AlertCircle className="w-4 h-4 text-amber-600" />
                    <span className="text-sm font-medium text-amber-800 dark:text-amber-200">
                      Multi-file Model (SD3/FLUX/Wan2)
                    </span>
                  </div>
                  <div className="grid grid-cols-2 gap-3">
                    <div>
                      <label className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1 block">VAE Path</label>
                      <input
                        type="text"
                        value={vaePath}
                        onChange={(e) => setVaePath(e.target.value)}
                        placeholder="path/to/vae.safetensors"
                        className="w-full px-2 py-1.5 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                      />
                    </div>
                    <div>
                      <label className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1 block">T5-XXL / UMT5 Path</label>
                      <input
                        type="text"
                        value={t5xxlPath}
                        onChange={(e) => setT5xxlPath(e.target.value)}
                        placeholder="path/to/t5xxl.gguf"
                        className="w-full px-2 py-1.5 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                      />
                    </div>
                    <div>
                      <label className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1 block">CLIP-L Path (SD3)</label>
                      <input
                        type="text"
                        value={clipLPath}
                        onChange={(e) => setClipLPath(e.target.value)}
                        placeholder="path/to/clip_l.safetensors"
                        className="w-full px-2 py-1.5 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                      />
                    </div>
                    <div>
                      <label className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1 block">CLIP-G Path (SD3/SDXL)</label>
                      <input
                        type="text"
                        value={clipGPath}
                        onChange={(e) => setClipGPath(e.target.value)}
                        placeholder="path/to/clip_g.safetensors"
                        className="w-full px-2 py-1.5 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                      />
                    </div>
                  </div>
                  <p className="text-xs text-amber-700 dark:text-amber-300 mt-2">
                    Leave empty for single-file models (SD1.5, SDXL). Required for SD3, FLUX, Wan2.
                  </p>
                </div>
              )}

              {/* Vision Model Options */}
              {filePath && (detectModelType(filePath) === 'vision' || modelType === 'vision') && (
                <div className="p-4 rounded-xl bg-purple-50 dark:bg-purple-900/20 border border-purple-200 dark:border-purple-800">
                  <div className="flex items-center gap-2 mb-3">
                    <Eye className="w-4 h-4 text-purple-600" />
                    <span className="text-sm font-medium text-purple-800 dark:text-purple-200">
                      Vision Model Options
                    </span>
                  </div>
                  <div>
                    <label className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1 block">
                      Vision Projector (mmproj) Path
                    </label>
                    <input
                      type="text"
                      value={mmprojPath}
                      onChange={(e) => setMmprojPath(e.target.value)}
                      placeholder="path/to/mmproj-model.gguf"
                      className="w-full px-2 py-1.5 text-sm rounded border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
                    />
                  </div>
                  <p className="text-xs text-purple-700 dark:text-purple-300 mt-2">
                    Required for Gemma-3, Qwen-VL, LLaVA, and other vision-language models.
                  </p>
                </div>
              )}

              {/* Selected Model Summary */}
              {filePath && (
                <div className="flex items-center gap-3 p-3 rounded-lg bg-brand-50 dark:bg-brand-900/20 border border-brand-200 dark:border-brand-800">
                  <CheckCircle2 className="w-5 h-5 text-brand-500 flex-shrink-0" />
                  <div className="flex-1 min-w-0">
                    <p className="text-sm font-medium text-brand-700 dark:text-brand-300 truncate">
                      {filePath.split('/').pop()}
                    </p>
                    <p className="text-xs text-brand-600 dark:text-brand-400">
                      ID: {modelId} | Type: {getModelTypeLabel(modelType === 'auto' ? detectModelType(filePath) : modelType)}
                    </p>
                  </div>
                </div>
              )}

              {/* Actions */}
              <div className="flex justify-between items-center pt-4 border-t border-surface-200 dark:border-surface-700">
                <select
                  className="px-3 py-2 text-sm rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800"
                  value={strategy}
                  onChange={(e) => setStrategy(e.target.value)}
                >
                  <option value="balanced">Balanced Memory</option>
                  <option value="aggressive">Aggressive (More RAM)</option>
                  <option value="conservative">Conservative (Less RAM)</option>
                </select>
                <div className="flex gap-2">
                  <Button variant="secondary" onClick={() => {
                    setShowLoadForm(false);
                    setVaePath('');
                    setT5xxlPath('');
                    setClipLPath('');
                    setClipGPath('');
                    setMmprojPath('');
                  }}>
                    Cancel
                  </Button>
                  <Button
                    variant="primary"
                    onClick={() => loadMutation.mutate({
                      model_id: modelId,
                      file_path: filePath,
                      strategy,
                      model_type: modelType,
                      vae_path: vaePath || undefined,
                      t5xxl_path: t5xxlPath || undefined,
                      clip_l_path: clipLPath || undefined,
                      clip_g_path: clipGPath || undefined,
                      mmproj_path: mmprojPath || undefined,
                    })}
                    disabled={!filePath || loadMutation.isPending}
                    className="bg-gradient-to-r from-brand-500 to-brand-600 hover:from-brand-600 hover:to-brand-700"
                  >
                    {loadMutation.isPending ? (
                      <><Loader className="w-4 h-4 animate-spin" /> Loading...</>
                    ) : (
                      <><Play className="w-4 h-4" /> Load Model</>
                    )}
                  </Button>
                </div>
              </div>
            </div>
          </Modal>
        )}
      </AnimatePresence>

      {/* Runtime Config Modal */}
      <RuntimeConfigModal
        isOpen={showRuntimeConfig}
        onClose={() => setShowRuntimeConfig(false)}
        onApply={(config) => {
          console.log('Applying runtime config:', config);
          setNotification({ type: 'success', message: 'Runtime configuration updated!' });
          setTimeout(() => setNotification(null), 3000);
        }}
      />

      {/* Workspace Manager Modal */}
      <Modal
        isOpen={showWorkspaceManager}
        onClose={() => setShowWorkspaceManager(false)}
        title="Workspace Manager"
        size="lg"
      >
        <div className="space-y-5">
          {/* Workspace Path Info */}
          <div className="p-4 rounded-xl bg-surface-50 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
            <div className="flex items-center gap-2 mb-2">
              <HardDrive className="w-5 h-5 text-brand-500" />
              <span className="text-sm font-medium text-surface-700 dark:text-surface-300">
                SnapLLM Workspace
              </span>
            </div>
            <code className="text-xs text-surface-600 dark:text-surface-400 block p-2 rounded bg-white dark:bg-surface-900 border border-surface-200 dark:border-surface-700">
              {getDefaultWorkspacePath()}
            </code>
          </div>

          {/* Workspace Folders List */}
          <div>
            <div className="flex items-center justify-between mb-3">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                Workspace Folders
              </label>
              <IconButton
                icon={<RefreshCw className={clsx("w-4 h-4", isLoadingWorkspace && "animate-spin")} />}
                label="Refresh"
                size="sm"
                onClick={loadWorkspaceFolders}
              />
            </div>

            {isLoadingWorkspace ? (
              <div className="flex items-center justify-center p-8 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800">
                <Loader className="w-5 h-5 animate-spin text-brand-500 mr-2" />
                <span className="text-sm text-surface-500">Loading workspace info...</span>
              </div>
            ) : (
              <div className="space-y-2">
                {workspaceFolders.map(folder => (
                  <div
                    key={folder.path}
                    className={clsx(
                      'flex items-center gap-3 p-4 rounded-lg border transition-all',
                      folder.exists
                        ? 'border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900'
                        : 'border-dashed border-surface-300 dark:border-surface-600 bg-surface-50 dark:bg-surface-800 opacity-60'
                    )}
                  >
                    <div className={clsx(
                      'w-10 h-10 rounded-lg flex items-center justify-center flex-shrink-0',
                      folder.exists
                        ? folder.name.includes('vPID') ? 'bg-ai-purple/20' : 'bg-brand-100 dark:bg-brand-900/30'
                        : 'bg-surface-100 dark:bg-surface-800'
                    )}>
                      {folder.name.includes('vPID') ? (
                        <Zap className={clsx('w-5 h-5', folder.exists ? 'text-ai-purple' : 'text-surface-400')} />
                      ) : folder.name.includes('Models') ? (
                        <Cpu className={clsx('w-5 h-5', folder.exists ? 'text-brand-600 dark:text-brand-400' : 'text-surface-400')} />
                      ) : folder.name.includes('Logs') ? (
                        <FileText className={clsx('w-5 h-5', folder.exists ? 'text-warning-600 dark:text-warning-400' : 'text-surface-400')} />
                      ) : (
                        <Settings2 className={clsx('w-5 h-5', folder.exists ? 'text-success-600 dark:text-success-400' : 'text-surface-400')} />
                      )}
                    </div>

                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <p className="font-medium text-surface-900 dark:text-white">
                          {folder.name}
                        </p>
                        {folder.exists ? (
                          <Badge variant="success" size="sm">
                            {folder.files} {folder.files === 1 ? 'item' : 'items'}
                          </Badge>
                        ) : (
                          <Badge variant="default" size="sm">Not created</Badge>
                        )}
                      </div>
                      <p className="text-xs text-surface-500 truncate">{folder.path}</p>
                    </div>

                    {folder.exists && (
                      <Button
                        variant="danger"
                        size="sm"
                        onClick={() => setConfirmDeleteFolder(folder)}
                        disabled={deletingFolder === folder.path}
                      >
                        {deletingFolder === folder.path ? (
                          <Loader className="w-4 h-4 animate-spin" />
                        ) : (
                          <Trash2 className="w-4 h-4" />
                        )}
                        Delete
                      </Button>
                    )}
                  </div>
                ))}

                {workspaceFolders.length === 0 && (
                  <div className="text-center py-8 text-surface-500">
                    <FolderOpen className="w-10 h-10 mx-auto mb-2 opacity-50" />
                    <p className="text-sm">No workspace folders found</p>
                  </div>
                )}
              </div>
            )}
          </div>

          {/* Warning */}
          <div className="p-3 rounded-lg bg-warning-50 dark:bg-warning-900/20 border border-warning-200 dark:border-warning-800">
            <div className="flex items-start gap-2">
              <AlertTriangle className="w-4 h-4 text-warning-600 dark:text-warning-400 mt-0.5 flex-shrink-0" />
              <div>
                <p className="text-sm font-medium text-warning-800 dark:text-warning-200">
                  Warning
                </p>
                <p className="text-xs text-warning-700 dark:text-warning-300 mt-1">
                  Deleting workspace folders will permanently remove cached tensors and data.
                  This action cannot be undone.
                </p>
              </div>
            </div>
          </div>

          {/* Close Button */}
          <div className="flex justify-end pt-4 border-t border-surface-200 dark:border-surface-700">
            <Button variant="secondary" onClick={() => setShowWorkspaceManager(false)}>
              Close
            </Button>
          </div>
        </div>
      </Modal>

      {/* Delete Confirmation Modal */}
      <Modal
        isOpen={!!confirmDeleteFolder}
        onClose={() => setConfirmDeleteFolder(null)}
        title="Confirm Delete"
        size="sm"
      >
        {confirmDeleteFolder && (
          <div className="space-y-4">
            <div className="p-4 rounded-lg bg-error-50 dark:bg-error-900/20 border border-error-200 dark:border-error-800">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 rounded-lg bg-error-100 dark:bg-error-900/30 flex items-center justify-center flex-shrink-0">
                  <Trash2 className="w-5 h-5 text-error-600 dark:text-error-400" />
                </div>
                <div>
                  <p className="font-medium text-error-800 dark:text-error-200">
                    Delete {confirmDeleteFolder.name}?
                  </p>
                  <p className="text-sm text-error-700 dark:text-error-300">
                    {confirmDeleteFolder.files} {confirmDeleteFolder.files === 1 ? 'item' : 'items'} will be deleted
                  </p>
                </div>
              </div>
            </div>

            <p className="text-sm text-surface-600 dark:text-surface-400">
              This will permanently delete all files in:
            </p>
            <code className="text-xs text-surface-600 dark:text-surface-400 block p-2 rounded bg-surface-100 dark:bg-surface-800 border border-surface-200 dark:border-surface-700">
              {confirmDeleteFolder.path}
            </code>

            <div className="flex justify-end gap-2 pt-4 border-t border-surface-200 dark:border-surface-700">
              <Button variant="secondary" onClick={() => setConfirmDeleteFolder(null)}>
                Cancel
              </Button>
              <Button
                variant="danger"
                onClick={() => handleDeleteFolder(confirmDeleteFolder)}
                disabled={deletingFolder === confirmDeleteFolder.path}
              >
                {deletingFolder === confirmDeleteFolder.path ? (
                  <>
                    <Loader className="w-4 h-4 animate-spin" />
                    Deleting...
                  </>
                ) : (
                  <>
                    <Trash2 className="w-4 h-4" />
                    Delete Permanently
                  </>
                )}
              </Button>
            </div>
          </div>
        )}
      </Modal>

      {/* Model Specifications Panel */}
      <AnimatePresence>
        {selectedModel && (
          <ModelSpecificationsPanel
            model={selectedModel}
            onClose={() => setSelectedModel(null)}
          />
        )}
      </AnimatePresence>
    </div>
  );
}
