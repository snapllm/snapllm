// ============================================================================
// SnapLLM Enterprise - Visual Workflow Builder
// Multi-Modal AI Pipeline Orchestration
// ============================================================================

import React, { useState, useCallback } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Workflow,
  Plus,
  Play,
  Pause,
  Save,
  Download,
  Upload,
  Settings2,
  Trash2,
  Copy,
  Edit3,
  Eye,
  Zap,
  Brain,
  GitBranch,
  ArrowRight,
  MessageSquare,
  Image as ImageIcon,
  Mic,
  Database,
  Code2,
  FileText,
  Globe,
  Filter,
  Split,
  Merge,
  Clock,
  CheckCircle2,
  XCircle,
  Loader2,
  MoreVertical,
  ChevronRight,
  Layers,
  Box,
  Cpu,
} from 'lucide-react';
import { Button, IconButton, Badge, Card, Progress } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface WorkflowNode {
  id: string;
  type: 'input' | 'model' | 'transform' | 'condition' | 'output' | 'merge';
  name: string;
  config: Record<string, any>;
  position: { x: number; y: number };
}

interface WorkflowEdge {
  id: string;
  source: string;
  target: string;
  label?: string;
}

interface WorkflowDef {
  id: string;
  name: string;
  description: string;
  nodes: WorkflowNode[];
  edges: WorkflowEdge[];
  status: 'draft' | 'active' | 'running' | 'error';
  lastRun?: Date;
  runsCount: number;
  avgDuration: number;
  createdAt: Date;
}

// ============================================================================
// Node Types
// ============================================================================

const NODE_TYPES = [
  { type: 'input', name: 'Input', icon: Box, color: 'brand', description: 'Start of workflow' },
  { type: 'model', name: 'AI Model', icon: Brain, color: 'ai-purple', description: 'Run AI inference' },
  { type: 'transform', name: 'Transform', icon: Filter, color: 'warning', description: 'Transform data' },
  { type: 'condition', name: 'Condition', icon: GitBranch, color: 'error', description: 'Branch logic' },
  { type: 'merge', name: 'Merge', icon: Merge, color: 'success', description: 'Combine outputs' },
  { type: 'output', name: 'Output', icon: ArrowRight, color: 'surface', description: 'End of workflow' },
];

const MODEL_TYPES = [
  { id: 'llm', name: 'Text Generation', icon: MessageSquare },
  { id: 'vision', name: 'Vision Analysis', icon: Eye },
  { id: 'image', name: 'Image Generation', icon: ImageIcon },
  { id: 'audio', name: 'Speech/Audio', icon: Mic },
  { id: 'embedding', name: 'Embedding', icon: Database },
];

// ============================================================================
// Sample Workflows
// ============================================================================

const SAMPLE_WORKFLOWS: WorkflowDef[] = [
  {
    id: '1',
    name: 'Document Q&A Pipeline',
    description: 'RAG pipeline for document question answering',
    nodes: [],
    edges: [],
    status: 'active',
    lastRun: new Date(),
    runsCount: 156,
    avgDuration: 2.3,
    createdAt: new Date('2024-01-15'),
  },
  {
    id: '2',
    name: 'Image-to-Text Pipeline',
    description: 'Vision model to analyze images and generate descriptions',
    nodes: [],
    edges: [],
    status: 'active',
    lastRun: new Date(),
    runsCount: 89,
    avgDuration: 4.5,
    createdAt: new Date('2024-02-01'),
  },
  {
    id: '3',
    name: 'Multi-Modal Content Generator',
    description: 'Generate text, images, and audio from a single prompt',
    nodes: [],
    edges: [],
    status: 'draft',
    runsCount: 0,
    avgDuration: 0,
    createdAt: new Date('2024-03-01'),
  },
];

// ============================================================================
// Main Component
// ============================================================================

export default function Workflows() {
  const [workflows, setWorkflows] = useState<WorkflowDef[]>(SAMPLE_WORKFLOWS);
  const [selectedWorkflow, setSelectedWorkflow] = useState<WorkflowDef | null>(null);
  const [showBuilder, setShowBuilder] = useState(false);

  const getStatusColor = (status: WorkflowDef['status']) => {
    switch (status) {
      case 'active': return 'success';
      case 'running': return 'warning';
      case 'draft': return 'default';
      case 'error': return 'error';
    }
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Workflow Builder
          </h1>
          <p className="text-surface-500 mt-1">
            Create visual AI pipelines with multi-modal support
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="secondary">
            <Upload className="w-4 h-4" />
            Import
          </Button>
          <Button variant="primary" onClick={() => setShowBuilder(true)}>
            <Plus className="w-4 h-4" />
            New Workflow
          </Button>
        </div>
      </div>

      {/* Stats */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Workflow className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">{workflows.length}</p>
              <p className="text-sm text-surface-500">Total Workflows</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <CheckCircle2 className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {workflows.filter(w => w.status === 'active').length}
              </p>
              <p className="text-sm text-surface-500">Active</p>
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
                {workflows.reduce((sum, w) => sum + w.runsCount, 0)}
              </p>
              <p className="text-sm text-surface-500">Total Runs</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Clock className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {(workflows.reduce((sum, w) => sum + w.avgDuration, 0) / workflows.length || 0).toFixed(1)}s
              </p>
              <p className="text-sm text-surface-500">Avg Duration</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Workflows List */}
      <Card className="p-6">
        <div className="flex items-center justify-between mb-4">
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
            Your Workflows
          </h2>
        </div>

        <div className="space-y-3">
          {workflows.map(workflow => (
            <motion.div
              key={workflow.id}
              whileHover={{ scale: 1.01 }}
              className="flex items-center gap-4 p-4 rounded-xl border border-surface-200 dark:border-surface-700 hover:border-brand-300 dark:hover:border-brand-700 cursor-pointer transition-colors"
              onClick={() => {
                setSelectedWorkflow(workflow);
                setShowBuilder(true);
              }}
            >
              <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center">
                <Workflow className="w-6 h-6 text-brand-600 dark:text-brand-400" />
              </div>

              <div className="flex-1 min-w-0">
                <div className="flex items-center gap-2">
                  <h3 className="font-semibold text-surface-900 dark:text-white">
                    {workflow.name}
                  </h3>
                  <Badge variant={getStatusColor(workflow.status)} size="sm">
                    {workflow.status}
                  </Badge>
                </div>
                <p className="text-sm text-surface-500 truncate">
                  {workflow.description}
                </p>
              </div>

              <div className="flex items-center gap-6 text-sm">
                <div className="text-center">
                  <p className="font-semibold text-surface-900 dark:text-white">{workflow.runsCount}</p>
                  <p className="text-xs text-surface-500">Runs</p>
                </div>
                <div className="text-center">
                  <p className="font-semibold text-surface-900 dark:text-white">{workflow.avgDuration}s</p>
                  <p className="text-xs text-surface-500">Avg Time</p>
                </div>
                {workflow.lastRun && (
                  <div className="text-center">
                    <p className="font-semibold text-surface-900 dark:text-white">
                      {workflow.lastRun.toLocaleDateString()}
                    </p>
                    <p className="text-xs text-surface-500">Last Run</p>
                  </div>
                )}
              </div>

              <div className="flex items-center gap-2">
                <Button variant="ghost" size="sm">
                  <Play className="w-4 h-4" />
                </Button>
                <IconButton icon={<Settings2 className="w-4 h-4" />} label="Settings" />
                <IconButton icon={<MoreVertical className="w-4 h-4" />} label="More" />
              </div>
            </motion.div>
          ))}
        </div>
      </Card>

      {/* Node Types Reference */}
      <div className="grid grid-cols-2 gap-6">
        <Card className="p-6">
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
            Node Types
          </h2>
          <div className="grid grid-cols-3 gap-3">
            {NODE_TYPES.map(node => (
              <div
                key={node.type}
                className="flex items-center gap-3 p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
              >
                <div className={clsx(
                  'w-10 h-10 rounded-lg flex items-center justify-center',
                  `bg-${node.color}-100 dark:bg-${node.color}-900/30`
                )}>
                  <node.icon className={`w-5 h-5 text-${node.color}-600 dark:text-${node.color}-400`} />
                </div>
                <div>
                  <p className="text-sm font-medium text-surface-900 dark:text-white">{node.name}</p>
                  <p className="text-xs text-surface-500">{node.description}</p>
                </div>
              </div>
            ))}
          </div>
        </Card>

        <Card className="p-6">
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
            Model Types
          </h2>
          <div className="grid grid-cols-3 gap-3">
            {MODEL_TYPES.map(model => (
              <div
                key={model.id}
                className="flex items-center gap-3 p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
              >
                <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
                  <model.icon className="w-5 h-5 text-ai-purple" />
                </div>
                <p className="text-sm font-medium text-surface-900 dark:text-white">{model.name}</p>
              </div>
            ))}
          </div>
        </Card>
      </div>

      {/* Templates */}
      <Card className="p-6">
        <h2 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
          Workflow Templates
        </h2>
        <div className="grid grid-cols-4 gap-4">
          {[
            { name: 'RAG Pipeline', description: 'Document retrieval and Q&A', icon: Database },
            { name: 'Image Analysis', description: 'Vision model pipeline', icon: Eye },
            { name: 'Content Generation', description: 'Multi-modal content creation', icon: Layers },
            { name: 'Data Processing', description: 'ETL and transformation', icon: Filter },
          ].map((template, i) => (
            <button
              key={i}
              className="p-4 rounded-xl border border-surface-200 dark:border-surface-700 hover:border-brand-500 dark:hover:border-brand-500 text-left transition-colors"
            >
              <div className="w-10 h-10 rounded-lg bg-surface-100 dark:bg-surface-800 flex items-center justify-center mb-3">
                <template.icon className="w-5 h-5 text-surface-500" />
              </div>
              <p className="font-medium text-surface-900 dark:text-white">{template.name}</p>
              <p className="text-sm text-surface-500">{template.description}</p>
            </button>
          ))}
        </div>
      </Card>
    </div>
  );
}
