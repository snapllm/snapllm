// ============================================================================
// SnapLLM - AI Agents
// Autonomous Multi-Modal AI Agents with Tool Use, Workflows, and Analytics
// Comprehensive Implementation with Validation and Persistence
// ============================================================================

import { useState, useEffect, useMemo } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Bot,
  Plus,
  Play,
  Square,
  Settings2,
  Trash2,
  Copy,
  Eye,
  Brain,
  Wrench,
  Globe,
  Database,
  Code2,
  FileSearch,
  Calculator,
  Mail,
  Calendar,
  MessageSquare,
  Image as ImageIcon,
  Mic,
  CheckCircle2,
  XCircle,
  Clock,
  Activity,
  TrendingUp,
  Target,
  Cpu,
  Search,
  SortAsc,
  SortDesc,
  Grid3X3,
  List,
  Star,
  StarOff,
  Loader2,
  X,
  RotateCcw,
  Download,
  History,
  GitBranch,
  Terminal,
  RefreshCw,
  Workflow,
  Timer,
  BarChart3,
  Sliders,
  Hash,
} from 'lucide-react';
import { Button, IconButton, Badge, Card, Toggle } from '../components/ui';
import {
  AreaChart,
  Area,
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  Legend,
} from 'recharts';

// ============================================================================
// Validation Utilities
// ============================================================================

const validateNumber = (value: any, fallback: number = 0): number => {
  const num = Number(value);
  return isNaN(num) || !isFinite(num) ? fallback : num;
};

const validateString = (value: any, fallback: string = ''): string => {
  return typeof value === 'string' ? value : fallback;
};

const validateArray = <T,>(value: any, fallback: T[] = []): T[] => {
  return Array.isArray(value) ? value : fallback;
};

const validateDate = (value: any, fallback: Date = new Date()): Date => {
  if (value instanceof Date && !isNaN(value.getTime())) return value;
  if (typeof value === 'string' || typeof value === 'number') {
    const date = new Date(value);
    return isNaN(date.getTime()) ? fallback : date;
  }
  return fallback;
};

// ============================================================================
// Types
// ============================================================================

type ToolCategory = 'search' | 'code' | 'data' | 'communication' | 'utility' | 'ai';
type AgentStatus = 'active' | 'idle' | 'running' | 'error' | 'paused';
type Capability = 'text' | 'vision' | 'audio' | 'code' | 'web' | 'files';
type RunStatus = 'pending' | 'running' | 'completed' | 'failed' | 'cancelled';

interface Tool {
  id: string;
  name: string;
  description: string;
  icon: string;
  category: ToolCategory;
  enabled: boolean;
  config?: Record<string, any>;
}

interface Agent {
  id: string;
  name: string;
  description: string;
  avatar: string;
  modelId: string;
  capabilities: Capability[];
  tools: string[]; // Tool IDs
  systemPrompt: string;
  status: AgentStatus;
  stats: {
    tasksCompleted: number;
    avgResponseTime: number;
    successRate: number;
    totalTokens: number;
  };
  createdAt: Date;
  updatedAt: Date;
  isFavorite?: boolean;
  temperature?: number;
  maxTokens?: number;
}

interface AgentRun {
  id: string;
  agentId: string;
  agentName: string;
  task: string;
  status: RunStatus;
  result?: string;
  error?: string;
  startedAt: Date;
  completedAt?: Date;
  durationMs?: number;
  tokensUsed?: number;
  steps: RunStep[];
}

interface RunStep {
  id: string;
  type: 'thought' | 'tool_call' | 'tool_result' | 'response';
  content: string;
  timestamp: Date;
  toolId?: string;
  toolInput?: string;
  toolOutput?: string;
}

interface Workflow {
  id: string;
  name: string;
  description: string;
  steps: WorkflowStep[];
  createdAt: Date;
  updatedAt: Date;
  runsCount: number;
  lastRunAt?: Date;
}

interface WorkflowStep {
  id: string;
  agentId: string;
  task: string;
  order: number;
  dependsOn?: string[];
  timeout?: number;
}

interface AgentSettings {
  defaultModel: string;
  defaultTemperature: number;
  defaultMaxTokens: number;
  autoSave: boolean;
  showAnalytics: boolean;
  confirmDelete: boolean;
  defaultTools: string[];
}

// ============================================================================
// Constants
// ============================================================================

const AVAILABLE_TOOLS: Tool[] = [
  { id: 'web-search', name: 'Web Search', description: 'Search the internet for information', icon: 'Globe', category: 'search', enabled: true },
  { id: 'code-exec', name: 'Code Execution', description: 'Run Python/JavaScript code', icon: 'Code2', category: 'code', enabled: true },
  { id: 'file-search', name: 'File Search', description: 'Search through documents and files', icon: 'FileSearch', category: 'search', enabled: true },
  { id: 'calculator', name: 'Calculator', description: 'Perform mathematical calculations', icon: 'Calculator', category: 'utility', enabled: true },
  { id: 'database', name: 'Database Query', description: 'Query SQL and NoSQL databases', icon: 'Database', category: 'data', enabled: true },
  { id: 'email', name: 'Email', description: 'Send and read emails', icon: 'Mail', category: 'communication', enabled: false },
  { id: 'calendar', name: 'Calendar', description: 'Manage calendar events', icon: 'Calendar', category: 'communication', enabled: false },
  { id: 'image-gen', name: 'Image Generation', description: 'Generate images from text', icon: 'ImageIcon', category: 'ai', enabled: true },
  { id: 'vision', name: 'Vision Analysis', description: 'Analyze images and documents', icon: 'Eye', category: 'ai', enabled: true },
  { id: 'terminal', name: 'Terminal', description: 'Execute shell commands', icon: 'Terminal', category: 'code', enabled: false },
];

const TOOL_ICONS: Record<string, React.ComponentType<{ className?: string }>> = {
  Globe,
  Code2,
  FileSearch,
  Calculator,
  Database,
  Mail,
  Calendar,
  ImageIcon,
  Eye,
  Terminal,
  Brain,
  Wrench,
};

const CAPABILITY_ICONS: Record<Capability, React.ComponentType<{ className?: string }>> = {
  text: MessageSquare,
  vision: ImageIcon,
  audio: Mic,
  code: Code2,
  web: Globe,
  files: FileSearch,
};

const AVATAR_OPTIONS = ['🤖', '🔬', '👨‍💻', '👁️', '🧠', '⚡', '🎯', '🚀', '💡', '🔧', '📊', '🎨'];

const MODEL_OPTIONS = [
  { id: 'llama-3-8b', name: 'Llama 3 8B', description: 'Fast, general purpose' },
  { id: 'llama-3-70b', name: 'Llama 3 70B', description: 'High quality, slower' },
  { id: 'codellama-34b', name: 'CodeLlama 34B', description: 'Optimized for code' },
  { id: 'llava-13b', name: 'LLaVA 13B', description: 'Vision + Language' },
  { id: 'mistral-7b', name: 'Mistral 7B', description: 'Balanced performance' },
  { id: 'mixtral-8x7b', name: 'Mixtral 8x7B', description: 'MoE architecture' },
];


const DEFAULT_SETTINGS: AgentSettings = {
  defaultModel: 'llama-3-8b',
  defaultTemperature: 0.7,
  defaultMaxTokens: 2048,
  autoSave: true,
  showAnalytics: true,
  confirmDelete: true,
  defaultTools: ['web-search', 'calculator', 'file-search'],
};

const STORAGE_KEYS = {
  agents: 'snapllm_agents',
  runs: 'snapllm_agent_runs',
  workflows: 'snapllm_workflows',
  settings: 'snapllm_agent_settings',
};

// ============================================================================
// Helper Functions
// ============================================================================

const generateId = (): string => {
  return `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
};

const formatDuration = (ms: number): string => {
  if (ms < 1000) return `${ms.toFixed(0)}ms`;
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`;
  return `${(ms / 60000).toFixed(1)}m`;
};

const formatRelativeTime = (date: Date): string => {
  const now = new Date();
  const diff = now.getTime() - date.getTime();
  const seconds = Math.floor(diff / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);

  if (days > 0) return `${days}d ago`;
  if (hours > 0) return `${hours}h ago`;
  if (minutes > 0) return `${minutes}m ago`;
  return 'Just now';
};

// ============================================================================
// Custom Tooltip Component
// ============================================================================

interface CustomTooltipProps {
  active?: boolean;
  payload?: any[];
  label?: string;
}

const CustomTooltip: React.FC<CustomTooltipProps> = ({ active, payload, label }) => {
  if (!active || !payload?.length) return null;

  return (
    <div className="bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 rounded-lg shadow-lg p-3">
      <p className="text-xs font-medium text-surface-600 dark:text-surface-400 mb-1">{label}</p>
      {payload.map((entry: any, index: number) => (
        <p key={index} className="text-sm" style={{ color: entry.color }}>
          {entry.name}: {typeof entry.value === 'number' ? entry.value.toLocaleString() : entry.value}
        </p>
      ))}
    </div>
  );
};

// ============================================================================
// Sub-Components
// ============================================================================

interface StatCardProps {
  title: string;
  value: string | number;
  subtitle?: string;
  icon: React.ReactNode;
  trend?: { value: number; label: string };
  color?: 'brand' | 'success' | 'warning' | 'error';
}

const StatCard: React.FC<StatCardProps> = ({ title, value, subtitle, icon, trend, color = 'brand' }) => {
  const colorClasses = {
    brand: 'from-brand-500 to-blue-600',
    success: 'from-emerald-500 to-teal-600',
    warning: 'from-amber-500 to-orange-600',
    error: 'from-red-500 to-rose-600',
  };

  return (
    <Card className="p-4">
      <div className="flex items-start justify-between mb-2">
        <div className={`w-10 h-10 rounded-lg bg-gradient-to-br ${colorClasses[color]} flex items-center justify-center`}>
          {icon}
        </div>
        {trend && (
          <div className={clsx(
            'flex items-center gap-1 text-xs font-medium',
            trend.value >= 0 ? 'text-success-600' : 'text-error-600'
          )}>
            <TrendingUp className={clsx('w-3 h-3', trend.value < 0 && 'rotate-180')} />
            {Math.abs(trend.value)}%
          </div>
        )}
      </div>
      <p className="text-2xl font-bold text-surface-900 dark:text-white">{value}</p>
      <p className="text-sm text-surface-500">{title}</p>
      {subtitle && <p className="text-xs text-surface-400 mt-1">{subtitle}</p>}
    </Card>
  );
};

interface AgentCardProps {
  agent: Agent;
  onClick: () => void;
  onToggleFavorite: () => void;
  onRun: () => void;
  onDelete: () => void;
}

const AgentCard: React.FC<AgentCardProps> = ({ agent, onClick, onToggleFavorite, onRun, onDelete }) => {
  const getStatusColor = (status: AgentStatus) => {
    switch (status) {
      case 'active': return 'success';
      case 'running': return 'warning';
      case 'idle': return 'default';
      case 'error': return 'error';
      case 'paused': return 'default';
    }
  };

  return (
    <motion.div
      whileHover={{ scale: 1.02 }}
      className="card-hover cursor-pointer relative group"
      onClick={onClick}
    >
      {agent.isFavorite && (
        <Star className="absolute top-3 right-3 w-4 h-4 fill-amber-400 text-amber-400" />
      )}
      <div className="p-6">
        {/* Header */}
        <div className="flex items-start justify-between mb-4">
          <div className="flex items-center gap-3">
            <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center text-2xl">
              {agent.avatar}
            </div>
            <div>
              <h3 className="font-semibold text-surface-900 dark:text-white">
                {agent.name}
              </h3>
              <Badge variant={getStatusColor(agent.status)} dot>
                {agent.status}
              </Badge>
            </div>
          </div>
          <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
            <IconButton
              icon={agent.isFavorite ? <Star className="w-4 h-4 fill-amber-400 text-amber-400" /> : <StarOff className="w-4 h-4" />}
              label={agent.isFavorite ? 'Remove favorite' : 'Add favorite'}
              onClick={(e) => {
                e.stopPropagation();
                onToggleFavorite();
              }}
            />
            <IconButton
              icon={<Play className="w-4 h-4" />}
              label="Run agent"
              onClick={(e) => {
                e.stopPropagation();
                onRun();
              }}
            />
            <IconButton
              icon={<Trash2 className="w-4 h-4" />}
              label="Delete"
              onClick={(e) => {
                e.stopPropagation();
                onDelete();
              }}
            />
          </div>
        </div>

        {/* Description */}
        <p className="text-sm text-surface-500 mb-4 line-clamp-2">
          {validateString(agent.description, 'No description')}
        </p>

        {/* Capabilities */}
        <div className="flex items-center gap-2 mb-4">
          <span className="text-xs text-surface-500">Capabilities:</span>
          <div className="flex items-center gap-1">
            {validateArray<Capability>(agent.capabilities).map(cap => {
              const Icon = CAPABILITY_ICONS[cap] || Brain;
              return (
                <div
                  key={cap}
                  className="w-6 h-6 rounded bg-surface-100 dark:bg-surface-800 flex items-center justify-center"
                  title={cap}
                >
                  <Icon className="w-3.5 h-3.5 text-surface-500" />
                </div>
              );
            })}
          </div>
        </div>

        {/* Tools */}
        <div className="flex items-center gap-2 mb-4">
          <Wrench className="w-4 h-4 text-surface-400" />
          <span className="text-sm text-surface-500">
            {validateArray(agent.tools).length} tools enabled
          </span>
        </div>

        {/* Model */}
        <div className="flex items-center gap-2 mb-4">
          <Cpu className="w-4 h-4 text-surface-400" />
          <span className="text-sm text-surface-500">
            {MODEL_OPTIONS.find(m => m.id === agent.modelId)?.name || agent.modelId}
          </span>
        </div>

        {/* Stats */}
        <div className="grid grid-cols-3 gap-2 pt-4 border-t border-surface-200 dark:border-surface-700">
          <div className="text-center">
            <p className="text-lg font-semibold text-surface-900 dark:text-white">
              {validateNumber(agent.stats.tasksCompleted)}
            </p>
            <p className="text-xs text-surface-500">Tasks</p>
          </div>
          <div className="text-center">
            <p className="text-lg font-semibold text-surface-900 dark:text-white">
              {validateNumber(agent.stats.avgResponseTime, 0).toFixed(1)}s
            </p>
            <p className="text-xs text-surface-500">Avg Time</p>
          </div>
          <div className="text-center">
            <p className="text-lg font-semibold text-surface-900 dark:text-white">
              {validateNumber(agent.stats.successRate)}%
            </p>
            <p className="text-xs text-surface-500">Success</p>
          </div>
        </div>
      </div>
    </motion.div>
  );
};

interface RunHistoryItemProps {
  run: AgentRun;
  onRerun: () => void;
  onView: () => void;
}

const RunHistoryItem: React.FC<RunHistoryItemProps> = ({ run, onRerun, onView }) => {
  const getStatusIcon = (status: RunStatus) => {
    switch (status) {
      case 'completed': return <CheckCircle2 className="w-4 h-4 text-success-500" />;
      case 'failed': return <XCircle className="w-4 h-4 text-error-500" />;
      case 'running': return <Loader2 className="w-4 h-4 text-brand-500 animate-spin" />;
      case 'pending': return <Clock className="w-4 h-4 text-surface-400" />;
      case 'cancelled': return <Square className="w-4 h-4 text-surface-400" />;
    }
  };

  return (
    <motion.div
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      className="flex items-center gap-4 p-4 rounded-lg bg-surface-50 dark:bg-surface-900 border border-surface-200 dark:border-surface-700 hover:border-surface-300 dark:hover:border-surface-600 transition-colors group"
    >
      {getStatusIcon(run.status)}
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2 mb-1">
          <span className="font-medium text-surface-900 dark:text-white truncate">
            {validateString(run.agentName, 'Unknown Agent')}
          </span>
          <Badge variant={run.status === 'completed' ? 'success' : run.status === 'failed' ? 'error' : run.status === 'running' ? 'warning' : 'default'}>
            {run.status}
          </Badge>
        </div>
        <p className="text-sm text-surface-500 truncate">{validateString(run.task)}</p>
        <div className="flex items-center gap-3 mt-1 text-xs text-surface-400">
          <span>{formatRelativeTime(validateDate(run.startedAt))}</span>
          {run.durationMs && <span>{formatDuration(run.durationMs)}</span>}
          {run.tokensUsed && <span>{run.tokensUsed.toLocaleString()} tokens</span>}
        </div>
      </div>
      <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
        <IconButton
          icon={<Eye className="w-4 h-4" />}
          label="View details"
          onClick={onView}
        />
        <IconButton
          icon={<RefreshCw className="w-4 h-4" />}
          label="Rerun"
          onClick={onRerun}
        />
      </div>
    </motion.div>
  );
};

// ============================================================================
// Main Component
// ============================================================================

export default function Agents() {
  // ============================================================================
  // State Management with localStorage Persistence
  // ============================================================================

  const [settings, setSettings] = useState<AgentSettings>(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEYS.settings);
      if (saved) {
        const parsed = JSON.parse(saved);
        return { ...DEFAULT_SETTINGS, ...parsed };
      }
    } catch (e) {
      console.warn('Failed to load agent settings:', e);
    }
    return DEFAULT_SETTINGS;
  });

  const [agents, setAgents] = useState<Agent[]>(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEYS.agents);
      if (saved) {
        const parsed = JSON.parse(saved);
        return validateArray(parsed).map((a: any) => ({
          ...a,
          createdAt: validateDate(a.createdAt),
          updatedAt: validateDate(a.updatedAt),
        }));
      }
    } catch (e) {
      console.warn('Failed to load agents:', e);
    }
    // Default agents
    return [
      {
        id: '1',
        name: 'Research Assistant',
        description: 'Helps with research, summarization, and fact-checking',
        avatar: '🔬',
        modelId: 'llama-3-8b',
        capabilities: ['text', 'web', 'files'] as Capability[],
        tools: ['web-search', 'file-search'],
        systemPrompt: 'You are a helpful research assistant. Help users find information, summarize content, and verify facts.',
        status: 'active' as AgentStatus,
        stats: { tasksCompleted: 156, avgResponseTime: 2.3, successRate: 94, totalTokens: 245000 },
        createdAt: new Date('2024-01-15'),
        updatedAt: new Date('2024-03-10'),
        isFavorite: true,
        temperature: 0.7,
        maxTokens: 2048,
      },
      {
        id: '2',
        name: 'Code Copilot',
        description: 'AI pair programmer for code generation and debugging',
        avatar: '👨‍💻',
        modelId: 'codellama-34b',
        capabilities: ['text', 'code'] as Capability[],
        tools: ['code-exec', 'file-search'],
        systemPrompt: 'You are an expert software engineer. Help users write, debug, and improve code.',
        status: 'idle' as AgentStatus,
        stats: { tasksCompleted: 89, avgResponseTime: 3.1, successRate: 87, totalTokens: 178000 },
        createdAt: new Date('2024-02-01'),
        updatedAt: new Date('2024-03-12'),
        isFavorite: false,
        temperature: 0.3,
        maxTokens: 4096,
      },
      {
        id: '3',
        name: 'Vision Analyst',
        description: 'Analyzes images and provides detailed descriptions',
        avatar: '👁️',
        modelId: 'llava-13b',
        capabilities: ['text', 'vision'] as Capability[],
        tools: ['vision'],
        systemPrompt: 'You are an expert image analyst. Describe and analyze images in detail.',
        status: 'idle' as AgentStatus,
        stats: { tasksCompleted: 234, avgResponseTime: 1.8, successRate: 92, totalTokens: 312000 },
        createdAt: new Date('2024-02-15'),
        updatedAt: new Date('2024-03-08'),
        isFavorite: false,
        temperature: 0.5,
        maxTokens: 1024,
      },
    ];
  });

  const [runs, setRuns] = useState<AgentRun[]>(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEYS.runs);
      if (saved) {
        const parsed = JSON.parse(saved);
        return validateArray(parsed).map((r: any) => ({
          ...r,
          startedAt: validateDate(r.startedAt),
          completedAt: r.completedAt ? validateDate(r.completedAt) : undefined,
          steps: validateArray(r.steps).map((s: any) => ({
            ...s,
            timestamp: validateDate(s.timestamp),
          })),
        }));
      }
    } catch (e) {
      console.warn('Failed to load runs:', e);
    }
    return [];
  });

  const [workflows, _setWorkflows] = useState<Workflow[]>(() => {
    try {
      const saved = localStorage.getItem(STORAGE_KEYS.workflows);
      if (saved) {
        const parsed = JSON.parse(saved);
        return validateArray(parsed).map((w: any) => ({
          ...w,
          createdAt: validateDate(w.createdAt),
          updatedAt: validateDate(w.updatedAt),
          lastRunAt: w.lastRunAt ? validateDate(w.lastRunAt) : undefined,
        }));
      }
    } catch (e) {
      console.warn('Failed to load workflows:', e);
    }
    return [];
  });

  const [activeTab, setActiveTab] = useState<'agents' | 'workflows' | 'runs' | 'analytics' | 'settings'>('agents');
  const [selectedAgent, setSelectedAgent] = useState<Agent | null>(null);
  const [selectedRun, setSelectedRun] = useState<AgentRun | null>(null);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [showRunModal, setShowRunModal] = useState(false);
  const [runAgent, setRunAgent] = useState<Agent | null>(null);
  const [taskInput, setTaskInput] = useState('');
  const [isRunning, setIsRunning] = useState(false);
  const [searchFilter, setSearchFilter] = useState('');
  const [sortOrder, setSortOrder] = useState<'name' | 'date' | 'tasks'>('date');
  const [sortDirection, setSortDirection] = useState<'asc' | 'desc'>('desc');
  const [viewMode, setViewMode] = useState<'grid' | 'list'>('grid');
  const [_editingAgent, _setEditingAgent] = useState<Agent | null>(null);

  // New agent form state
  const [newAgentForm, setNewAgentForm] = useState({
    name: '',
    description: '',
    avatar: '🤖',
    modelId: settings.defaultModel,
    capabilities: ['text'] as Capability[],
    tools: settings.defaultTools,
    systemPrompt: '',
    temperature: settings.defaultTemperature,
    maxTokens: settings.defaultMaxTokens,
  });

  // ============================================================================
  // Effects - Persist to localStorage
  // ============================================================================

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEYS.settings, JSON.stringify(settings));
    } catch (e) {
      console.warn('Failed to save settings:', e);
    }
  }, [settings]);

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEYS.agents, JSON.stringify(agents));
    } catch (e) {
      console.warn('Failed to save agents:', e);
    }
  }, [agents]);

  useEffect(() => {
    try {
      // Keep only last 100 runs
      const trimmed = runs.slice(0, 100);
      localStorage.setItem(STORAGE_KEYS.runs, JSON.stringify(trimmed));
    } catch (e) {
      console.warn('Failed to save runs:', e);
    }
  }, [runs]);

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEYS.workflows, JSON.stringify(workflows));
    } catch (e) {
      console.warn('Failed to save workflows:', e);
    }
  }, [workflows]);

  // ============================================================================
  // Computed Values
  // ============================================================================

  const filteredAgents = useMemo(() => {
    let filtered = [...agents];

    if (searchFilter) {
      const search = searchFilter.toLowerCase();
      filtered = filtered.filter(a =>
        a.name.toLowerCase().includes(search) ||
        a.description.toLowerCase().includes(search)
      );
    }

    filtered.sort((a, b) => {
      // Favorites first
      if (a.isFavorite && !b.isFavorite) return -1;
      if (!a.isFavorite && b.isFavorite) return 1;

      let comparison = 0;
      switch (sortOrder) {
        case 'name':
          comparison = a.name.localeCompare(b.name);
          break;
        case 'date':
          comparison = new Date(b.updatedAt).getTime() - new Date(a.updatedAt).getTime();
          break;
        case 'tasks':
          comparison = b.stats.tasksCompleted - a.stats.tasksCompleted;
          break;
      }
      return sortDirection === 'asc' ? -comparison : comparison;
    });

    return filtered;
  }, [agents, searchFilter, sortOrder, sortDirection]);

  const overallStats = useMemo(() => {
    const totalTasks = agents.reduce((sum, a) => sum + validateNumber(a.stats.tasksCompleted), 0);
    const avgSuccessRate = agents.length > 0
      ? agents.reduce((sum, a) => sum + validateNumber(a.stats.successRate), 0) / agents.length
      : 0;
    const totalTokens = agents.reduce((sum, a) => sum + validateNumber(a.stats.totalTokens), 0);
    const runningAgents = agents.filter(a => a.status === 'running').length;

    return { totalTasks, avgSuccessRate, totalTokens, runningAgents };
  }, [agents]);

  const runStats = useMemo(() => {
    const totalRuns = runs.length;
    const completedRuns = runs.filter(r => r.status === 'completed').length;
    const failedRuns = runs.filter(r => r.status === 'failed').length;
    const successRate = totalRuns > 0 ? (completedRuns / totalRuns) * 100 : 0;

    const avgDuration = runs.filter(r => r.durationMs).length > 0
      ? runs.filter(r => r.durationMs).reduce((sum, r) => sum + (r.durationMs || 0), 0) / runs.filter(r => r.durationMs).length
      : 0;

    return { totalRuns, completedRuns, failedRuns, successRate, avgDuration };
  }, [runs]);

  const runsTimelineData = useMemo(() => {
    const last7Days: { date: string; runs: number; success: number; failed: number }[] = [];

    for (let i = 6; i >= 0; i--) {
      const date = new Date();
      date.setDate(date.getDate() - i);
      const dateStr = date.toLocaleDateString('en-US', { weekday: 'short' });

      const dayRuns = runs.filter(r => {
        const rDate = new Date(r.startedAt);
        return rDate.toDateString() === date.toDateString();
      });

      last7Days.push({
        date: dateStr,
        runs: dayRuns.length,
        success: dayRuns.filter(r => r.status === 'completed').length,
        failed: dayRuns.filter(r => r.status === 'failed').length,
      });
    }

    return last7Days;
  }, [runs]);

  const agentUsageData = useMemo(() => {
    return agents.map(a => ({
      name: a.name,
      tasks: validateNumber(a.stats.tasksCompleted),
    })).sort((a, b) => b.tasks - a.tasks).slice(0, 5);
  }, [agents]);

  // ============================================================================
  // Handlers
  // ============================================================================

  const handleCreateAgent = () => {
    if (!newAgentForm.name.trim()) return;

    const newAgent: Agent = {
      id: generateId(),
      name: newAgentForm.name,
      description: newAgentForm.description,
      avatar: newAgentForm.avatar,
      modelId: newAgentForm.modelId,
      capabilities: newAgentForm.capabilities,
      tools: newAgentForm.tools,
      systemPrompt: newAgentForm.systemPrompt,
      status: 'idle',
      stats: { tasksCompleted: 0, avgResponseTime: 0, successRate: 0, totalTokens: 0 },
      createdAt: new Date(),
      updatedAt: new Date(),
      isFavorite: false,
      temperature: newAgentForm.temperature,
      maxTokens: newAgentForm.maxTokens,
    };

    setAgents(prev => [newAgent, ...prev]);
    setShowCreateModal(false);
    setNewAgentForm({
      name: '',
      description: '',
      avatar: '🤖',
      modelId: settings.defaultModel,
      capabilities: ['text'],
      tools: settings.defaultTools,
      systemPrompt: '',
      temperature: settings.defaultTemperature,
      maxTokens: settings.defaultMaxTokens,
    });
  };

  const handleDeleteAgent = (id: string) => {
    if (settings.confirmDelete && !window.confirm('Are you sure you want to delete this agent?')) {
      return;
    }
    setAgents(prev => prev.filter(a => a.id !== id));
    if (selectedAgent?.id === id) {
      setSelectedAgent(null);
    }
  };

  const handleToggleFavorite = (id: string) => {
    setAgents(prev => prev.map(a =>
      a.id === id ? { ...a, isFavorite: !a.isFavorite, updatedAt: new Date() } : a
    ));
  };

  const handleDuplicateAgent = (agent: Agent) => {
    const duplicated: Agent = {
      ...agent,
      id: generateId(),
      name: `${agent.name} (Copy)`,
      createdAt: new Date(),
      updatedAt: new Date(),
      stats: { tasksCompleted: 0, avgResponseTime: 0, successRate: 0, totalTokens: 0 },
      isFavorite: false,
    };
    setAgents(prev => [duplicated, ...prev]);
  };

  const handleRunAgent = (agent: Agent, task: string) => {
    if (!task.trim()) return;

    const newRun: AgentRun = {
      id: generateId(),
      agentId: agent.id,
      agentName: agent.name,
      task,
      status: 'running',
      startedAt: new Date(),
      steps: [],
    };

    setRuns(prev => [newRun, ...prev]);
    setIsRunning(true);

    // Update agent status
    setAgents(prev => prev.map(a =>
      a.id === agent.id ? { ...a, status: 'running' as AgentStatus } : a
    ));

    // Simulate execution
    const steps: RunStep[] = [];

    setTimeout(() => {
      steps.push({
        id: generateId(),
        type: 'thought',
        content: 'Analyzing the task and determining the best approach...',
        timestamp: new Date(),
      });
      setRuns(prev => prev.map(r => r.id === newRun.id ? { ...r, steps: [...steps] } : r));
    }, 500);

    setTimeout(() => {
      steps.push({
        id: generateId(),
        type: 'tool_call',
        content: 'Searching for relevant information...',
        timestamp: new Date(),
        toolId: 'web-search',
        toolInput: task,
      });
      setRuns(prev => prev.map(r => r.id === newRun.id ? { ...r, steps: [...steps] } : r));
    }, 1500);

    setTimeout(() => {
      steps.push({
        id: generateId(),
        type: 'tool_result',
        content: 'Found relevant information from search results.',
        timestamp: new Date(),
        toolId: 'web-search',
        toolOutput: 'Search results retrieved successfully.',
      });
      setRuns(prev => prev.map(r => r.id === newRun.id ? { ...r, steps: [...steps] } : r));
    }, 2500);

    setTimeout(() => {
      const success = Math.random() > 0.1;
      const endTime = new Date();
      const durationMs = endTime.getTime() - newRun.startedAt.getTime();
      const tokensUsed = Math.floor(Math.random() * 500) + 200;

      steps.push({
        id: generateId(),
        type: 'response',
        content: success
          ? `Based on my analysis, here is the response to your task: "${task}"...`
          : 'An error occurred while processing the task.',
        timestamp: endTime,
      });

      setRuns(prev => prev.map(r =>
        r.id === newRun.id
          ? {
              ...r,
              status: success ? 'completed' : 'failed',
              result: success ? `Completed task: ${task}` : undefined,
              error: success ? undefined : 'Processing error',
              completedAt: endTime,
              durationMs,
              tokensUsed,
              steps: [...steps],
            }
          : r
      ));

      // Update agent stats
      setAgents(prev => prev.map(a =>
        a.id === agent.id
          ? {
              ...a,
              status: 'idle' as AgentStatus,
              stats: {
                ...a.stats,
                tasksCompleted: a.stats.tasksCompleted + (success ? 1 : 0),
                avgResponseTime: ((a.stats.avgResponseTime * a.stats.tasksCompleted) + (durationMs / 1000)) / (a.stats.tasksCompleted + 1),
                successRate: Math.round(((a.stats.successRate * a.stats.tasksCompleted) + (success ? 100 : 0)) / (a.stats.tasksCompleted + 1)),
                totalTokens: a.stats.totalTokens + tokensUsed,
              },
              updatedAt: new Date(),
            }
          : a
      ));

      setIsRunning(false);
      setShowRunModal(false);
      setTaskInput('');
      setRunAgent(null);
    }, 4000);
  };

  const handleExportAgents = () => {
    const exportData = {
      agents,
      workflows,
      exportedAt: new Date().toISOString(),
    };

    const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `snapllm-agents-export-${new Date().toISOString().split('T')[0]}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const handleResetSettings = () => {
    setSettings(DEFAULT_SETTINGS);
  };

  // ============================================================================
  // Render
  // ============================================================================

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            AI Agents
          </h1>
          <p className="text-surface-500 mt-1">
            Create and manage autonomous AI agents with multi-modal capabilities
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="secondary" onClick={handleExportAgents}>
            <Download className="w-4 h-4 mr-1.5" />
            Export
          </Button>
          <Button variant="primary" onClick={() => setShowCreateModal(true)}>
            <Plus className="w-4 h-4 mr-1.5" />
            Create Agent
          </Button>
        </div>
      </div>

      {/* Stats */}
      <div className="grid grid-cols-4 gap-4">
        <StatCard
          title="Total Agents"
          value={agents.length}
          icon={<Bot className="w-5 h-5 text-white" />}
          color="brand"
        />
        <StatCard
          title="Running"
          value={overallStats.runningAgents}
          icon={<Activity className="w-5 h-5 text-white" />}
          color="success"
        />
        <StatCard
          title="Tasks Completed"
          value={overallStats.totalTasks.toLocaleString()}
          icon={<CheckCircle2 className="w-5 h-5 text-white" />}
          color="brand"
        />
        <StatCard
          title="Avg Success Rate"
          value={`${overallStats.avgSuccessRate.toFixed(0)}%`}
          icon={<Target className="w-5 h-5 text-white" />}
          color="warning"
        />
      </div>

      {/* Tabs */}
      <div className="flex items-center justify-between border-b border-surface-200 dark:border-surface-800">
        <div className="flex gap-1">
          {[
            { id: 'agents', label: 'Agents', icon: Bot },
            { id: 'workflows', label: 'Workflows', icon: GitBranch },
            { id: 'runs', label: 'Run History', icon: History },
            { id: 'analytics', label: 'Analytics', icon: BarChart3 },
            { id: 'settings', label: 'Settings', icon: Settings2 },
          ].map(tab => (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id as typeof activeTab)}
              className={clsx(
                'flex items-center gap-2 px-4 py-3 text-sm font-medium border-b-2 transition-colors',
                activeTab === tab.id
                  ? 'border-brand-500 text-brand-600 dark:text-brand-400'
                  : 'border-transparent text-surface-600 dark:text-surface-400 hover:text-surface-900 dark:hover:text-white'
              )}
            >
              <tab.icon className="w-4 h-4" />
              {tab.label}
            </button>
          ))}
        </div>
        {activeTab === 'agents' && (
          <div className="flex items-center gap-2 pb-2">
            <div className="relative">
              <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
              <input
                type="text"
                value={searchFilter}
                onChange={(e) => setSearchFilter(e.target.value)}
                placeholder="Search agents..."
                className="pl-9 pr-3 py-1.5 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500 w-48"
              />
            </div>
            <select
              value={sortOrder}
              onChange={(e) => setSortOrder(e.target.value as typeof sortOrder)}
              className="px-3 py-1.5 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
            >
              <option value="date">Date</option>
              <option value="name">Name</option>
              <option value="tasks">Tasks</option>
            </select>
            <IconButton
              icon={sortDirection === 'asc' ? <SortAsc className="w-4 h-4" /> : <SortDesc className="w-4 h-4" />}
              label="Toggle sort direction"
              onClick={() => setSortDirection(d => d === 'asc' ? 'desc' : 'asc')}
            />
            <div className="flex rounded-lg border border-surface-200 dark:border-surface-700 overflow-hidden">
              <button
                onClick={() => setViewMode('grid')}
                className={clsx(
                  'p-1.5',
                  viewMode === 'grid' ? 'bg-surface-100 dark:bg-surface-700' : 'hover:bg-surface-50 dark:hover:bg-surface-800'
                )}
              >
                <Grid3X3 className="w-4 h-4" />
              </button>
              <button
                onClick={() => setViewMode('list')}
                className={clsx(
                  'p-1.5',
                  viewMode === 'list' ? 'bg-surface-100 dark:bg-surface-700' : 'hover:bg-surface-50 dark:hover:bg-surface-800'
                )}
              >
                <List className="w-4 h-4" />
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Tab Content */}
      <AnimatePresence mode="wait">
        {/* Agents Tab */}
        {activeTab === 'agents' && (
          <motion.div
            key="agents"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
          >
            {filteredAgents.length === 0 ? (
              <div className="text-center py-12">
                <Bot className="w-12 h-12 mx-auto text-surface-300 dark:text-surface-600 mb-3" />
                <p className="text-surface-500">No agents found</p>
                <Button
                  variant="primary"
                  className="mt-4"
                  onClick={() => setShowCreateModal(true)}
                >
                  <Plus className="w-4 h-4 mr-1.5" />
                  Create Your First Agent
                </Button>
              </div>
            ) : viewMode === 'grid' ? (
              <div className="grid grid-cols-3 gap-4">
                {filteredAgents.map(agent => (
                  <AgentCard
                    key={agent.id}
                    agent={agent}
                    onClick={() => setSelectedAgent(agent)}
                    onToggleFavorite={() => handleToggleFavorite(agent.id)}
                    onRun={() => {
                      setRunAgent(agent);
                      setShowRunModal(true);
                    }}
                    onDelete={() => handleDeleteAgent(agent.id)}
                  />
                ))}

                {/* Create New Card */}
                <motion.button
                  whileHover={{ scale: 1.02 }}
                  onClick={() => setShowCreateModal(true)}
                  className="p-6 rounded-xl border-2 border-dashed border-surface-300 dark:border-surface-700 hover:border-brand-500 dark:hover:border-brand-500 flex flex-col items-center justify-center gap-3 text-surface-500 hover:text-brand-600 transition-colors min-h-[280px]"
                >
                  <Plus className="w-8 h-8" />
                  <span className="font-medium">Create New Agent</span>
                </motion.button>
              </div>
            ) : (
              <div className="space-y-2">
                {filteredAgents.map(agent => (
                  <motion.div
                    key={agent.id}
                    initial={{ opacity: 0, x: -20 }}
                    animate={{ opacity: 1, x: 0 }}
                    className="flex items-center gap-4 p-4 rounded-lg bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 hover:border-surface-300 dark:hover:border-surface-600 transition-colors cursor-pointer group"
                    onClick={() => setSelectedAgent(agent)}
                  >
                    <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center text-2xl flex-shrink-0">
                      {agent.avatar}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2 mb-1">
                        <h3 className="font-semibold text-surface-900 dark:text-white">{agent.name}</h3>
                        {agent.isFavorite && <Star className="w-4 h-4 fill-amber-400 text-amber-400" />}
                        <Badge variant={agent.status === 'active' ? 'success' : agent.status === 'running' ? 'warning' : 'default'} dot>
                          {agent.status}
                        </Badge>
                      </div>
                      <p className="text-sm text-surface-500 truncate">{agent.description}</p>
                    </div>
                    <div className="flex items-center gap-6 text-sm text-surface-500">
                      <div className="text-center">
                        <p className="font-semibold text-surface-900 dark:text-white">{agent.stats.tasksCompleted}</p>
                        <p className="text-xs">Tasks</p>
                      </div>
                      <div className="text-center">
                        <p className="font-semibold text-surface-900 dark:text-white">{agent.stats.successRate}%</p>
                        <p className="text-xs">Success</p>
                      </div>
                    </div>
                    <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
                      <IconButton
                        icon={<Play className="w-4 h-4" />}
                        label="Run"
                        onClick={(e) => {
                          e.stopPropagation();
                          setRunAgent(agent);
                          setShowRunModal(true);
                        }}
                      />
                      <IconButton
                        icon={<Copy className="w-4 h-4" />}
                        label="Duplicate"
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDuplicateAgent(agent);
                        }}
                      />
                      <IconButton
                        icon={<Trash2 className="w-4 h-4" />}
                        label="Delete"
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDeleteAgent(agent.id);
                        }}
                      />
                    </div>
                  </motion.div>
                ))}
              </div>
            )}
          </motion.div>
        )}

        {/* Workflows Tab */}
        {activeTab === 'workflows' && (
          <motion.div
            key="workflows"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
          >
            <div className="text-center py-12">
              <GitBranch className="w-12 h-12 mx-auto text-surface-300 dark:text-surface-600 mb-3" />
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
                Workflows Coming Soon
              </h3>
              <p className="text-surface-500 max-w-md mx-auto">
                Chain multiple agents together to create complex automation workflows.
                Define sequential or parallel tasks with dependencies.
              </p>
            </div>
          </motion.div>
        )}

        {/* Runs Tab */}
        {activeTab === 'runs' && (
          <motion.div
            key="runs"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
          >
            {/* Run Stats */}
            <div className="grid grid-cols-5 gap-4 mb-6">
              <StatCard
                title="Total Runs"
                value={runStats.totalRuns}
                icon={<Activity className="w-5 h-5 text-white" />}
                color="brand"
              />
              <StatCard
                title="Completed"
                value={runStats.completedRuns}
                icon={<CheckCircle2 className="w-5 h-5 text-white" />}
                color="success"
              />
              <StatCard
                title="Failed"
                value={runStats.failedRuns}
                icon={<XCircle className="w-5 h-5 text-white" />}
                color="error"
              />
              <StatCard
                title="Success Rate"
                value={`${runStats.successRate.toFixed(0)}%`}
                icon={<Target className="w-5 h-5 text-white" />}
                color="success"
              />
              <StatCard
                title="Avg Duration"
                value={formatDuration(runStats.avgDuration)}
                icon={<Timer className="w-5 h-5 text-white" />}
                color="warning"
              />
            </div>

            {/* Run List */}
            {runs.length === 0 ? (
              <div className="text-center py-12">
                <History className="w-12 h-12 mx-auto text-surface-300 dark:text-surface-600 mb-3" />
                <p className="text-surface-500">No run history yet</p>
                <p className="text-sm text-surface-400 mt-1">Run an agent to see execution history</p>
              </div>
            ) : (
              <div className="space-y-2">
                {runs.slice(0, 20).map(run => (
                  <RunHistoryItem
                    key={run.id}
                    run={run}
                    onView={() => setSelectedRun(run)}
                    onRerun={() => {
                      const agent = agents.find(a => a.id === run.agentId);
                      if (agent) {
                        setRunAgent(agent);
                        setTaskInput(run.task);
                        setShowRunModal(true);
                      }
                    }}
                  />
                ))}
              </div>
            )}
          </motion.div>
        )}

        {/* Analytics Tab */}
        {activeTab === 'analytics' && (
          <motion.div
            key="analytics"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
            className="space-y-6"
          >
            {/* Charts */}
            <div className="grid grid-cols-2 gap-6">
              {/* Runs Timeline */}
              <Card className="p-4">
                <h3 className="font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <TrendingUp className="w-4 h-4" />
                  Run Activity (7 days)
                </h3>
                <div className="h-64">
                  <ResponsiveContainer width="100%" height="100%">
                    <AreaChart data={runsTimelineData}>
                      <CartesianGrid strokeDasharray="3 3" stroke="currentColor" className="text-surface-200 dark:text-surface-700" />
                      <XAxis dataKey="date" tick={{ fontSize: 12 }} stroke="currentColor" className="text-surface-500" />
                      <YAxis tick={{ fontSize: 12 }} stroke="currentColor" className="text-surface-500" />
                      <Tooltip content={<CustomTooltip />} />
                      <Legend />
                      <Area type="monotone" dataKey="success" stackId="1" stroke="#10b981" fill="#10b981" fillOpacity={0.6} name="Success" />
                      <Area type="monotone" dataKey="failed" stackId="1" stroke="#ef4444" fill="#ef4444" fillOpacity={0.6} name="Failed" />
                    </AreaChart>
                  </ResponsiveContainer>
                </div>
              </Card>

              {/* Agent Usage */}
              <Card className="p-4">
                <h3 className="font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <BarChart3 className="w-4 h-4" />
                  Top Agents by Tasks
                </h3>
                <div className="h-64">
                  {agentUsageData.length > 0 ? (
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart data={agentUsageData} layout="vertical">
                        <CartesianGrid strokeDasharray="3 3" stroke="currentColor" className="text-surface-200 dark:text-surface-700" />
                        <XAxis type="number" tick={{ fontSize: 12 }} stroke="currentColor" className="text-surface-500" />
                        <YAxis dataKey="name" type="category" tick={{ fontSize: 12 }} stroke="currentColor" className="text-surface-500" width={100} />
                        <Tooltip content={<CustomTooltip />} />
                        <Bar dataKey="tasks" fill="#3b82f6" radius={[0, 4, 4, 0]} name="Tasks" />
                      </BarChart>
                    </ResponsiveContainer>
                  ) : (
                    <div className="h-full flex items-center justify-center text-surface-500">
                      No data yet
                    </div>
                  )}
                </div>
              </Card>
            </div>

            {/* Token Usage */}
            <Card className="p-4">
              <div className="flex items-center justify-between mb-4">
                <h3 className="font-semibold text-surface-900 dark:text-white flex items-center gap-2">
                  <Hash className="w-4 h-4" />
                  Total Token Usage
                </h3>
                <span className="text-2xl font-bold text-surface-900 dark:text-white">
                  {overallStats.totalTokens.toLocaleString()}
                </span>
              </div>
              <div className="grid grid-cols-3 gap-4">
                {agents.slice(0, 6).map(agent => (
                  <div key={agent.id} className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900">
                    <div className="flex items-center gap-2 mb-2">
                      <span className="text-lg">{agent.avatar}</span>
                      <span className="text-sm font-medium text-surface-900 dark:text-white truncate">{agent.name}</span>
                    </div>
                    <p className="text-lg font-bold text-surface-900 dark:text-white">
                      {validateNumber(agent.stats.totalTokens).toLocaleString()}
                    </p>
                    <p className="text-xs text-surface-500">tokens used</p>
                  </div>
                ))}
              </div>
            </Card>
          </motion.div>
        )}

        {/* Settings Tab */}
        {activeTab === 'settings' && (
          <motion.div
            key="settings"
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
            className="max-w-2xl space-y-6"
          >
            {/* Default Model */}
            <Card className="p-6">
              <h3 className="font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <Cpu className="w-5 h-5 text-surface-500" />
                Default Agent Settings
              </h3>
              <div className="space-y-4">
                <div>
                  <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                    Default Model
                  </label>
                  <select
                    value={settings.defaultModel}
                    onChange={(e) => setSettings(prev => ({ ...prev, defaultModel: e.target.value }))}
                    className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                  >
                    {MODEL_OPTIONS.map(model => (
                      <option key={model.id} value={model.id}>
                        {model.name} - {model.description}
                      </option>
                    ))}
                  </select>
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Default Temperature
                    </label>
                    <input
                      type="number"
                      value={settings.defaultTemperature}
                      onChange={(e) => setSettings(prev => ({ ...prev, defaultTemperature: parseFloat(e.target.value) || 0.7 }))}
                      min={0}
                      max={2}
                      step={0.1}
                      className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                    />
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Default Max Tokens
                    </label>
                    <input
                      type="number"
                      value={settings.defaultMaxTokens}
                      onChange={(e) => setSettings(prev => ({ ...prev, defaultMaxTokens: parseInt(e.target.value) || 2048 }))}
                      min={256}
                      max={8192}
                      className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                    />
                  </div>
                </div>
              </div>
            </Card>

            {/* Behavior */}
            <Card className="p-6">
              <h3 className="font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <Sliders className="w-5 h-5 text-surface-500" />
                Behavior
              </h3>
              <div className="space-y-4">
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">Auto Save</p>
                    <p className="text-xs text-surface-500">Automatically save agent changes</p>
                  </div>
                  <Toggle
                    checked={settings.autoSave}
                    onChange={(checked) => setSettings(prev => ({ ...prev, autoSave: checked }))}
                  />
                </div>
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">Show Analytics</p>
                    <p className="text-xs text-surface-500">Display analytics on agent cards</p>
                  </div>
                  <Toggle
                    checked={settings.showAnalytics}
                    onChange={(checked) => setSettings(prev => ({ ...prev, showAnalytics: checked }))}
                  />
                </div>
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">Confirm Delete</p>
                    <p className="text-xs text-surface-500">Ask for confirmation before deleting</p>
                  </div>
                  <Toggle
                    checked={settings.confirmDelete}
                    onChange={(checked) => setSettings(prev => ({ ...prev, confirmDelete: checked }))}
                  />
                </div>
              </div>
            </Card>

            {/* Actions */}
            <div className="flex items-center justify-between pt-4">
              <Button variant="secondary" onClick={handleResetSettings}>
                <RotateCcw className="w-4 h-4 mr-1.5" />
                Reset to Defaults
              </Button>
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Available Tools Section */}
      {activeTab === 'agents' && (
        <Card className="p-6">
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
            Available Tools
          </h2>
          <div className="grid grid-cols-5 gap-4">
            {AVAILABLE_TOOLS.map(tool => {
              const Icon = TOOL_ICONS[tool.icon] || Wrench;
              return (
                <div
                  key={tool.id}
                  className={clsx(
                    'flex items-center gap-3 p-3 rounded-lg',
                    tool.enabled ? 'bg-surface-50 dark:bg-surface-800' : 'bg-surface-100 dark:bg-surface-900 opacity-60'
                  )}
                >
                  <div className={clsx(
                    'w-10 h-10 rounded-lg flex items-center justify-center',
                    tool.enabled ? 'bg-surface-100 dark:bg-surface-700' : 'bg-surface-200 dark:bg-surface-800'
                  )}>
                    <Icon className="w-5 h-5 text-surface-500" />
                  </div>
                  <div className="flex-1 min-w-0">
                    <p className="text-sm font-medium text-surface-900 dark:text-white truncate">
                      {tool.name}
                    </p>
                    <p className="text-xs text-surface-500 truncate">{tool.description}</p>
                  </div>
                </div>
              );
            })}
          </div>
        </Card>
      )}

      {/* Create Agent Modal */}
      <AnimatePresence>
        {showCreateModal && (
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            className="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
            onClick={() => setShowCreateModal(false)}
          >
            <motion.div
              initial={{ scale: 0.95, opacity: 0 }}
              animate={{ scale: 1, opacity: 1 }}
              exit={{ scale: 0.95, opacity: 0 }}
              className="bg-white dark:bg-surface-800 rounded-xl shadow-xl w-full max-w-2xl m-4 max-h-[90vh] overflow-y-auto"
              onClick={(e) => e.stopPropagation()}
            >
              <div className="p-6 border-b border-surface-200 dark:border-surface-700">
                <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
                  Create New Agent
                </h2>
                <p className="text-sm text-surface-500 mt-1">
                  Configure your autonomous AI agent
                </p>
              </div>
              <div className="p-6 space-y-4">
                {/* Avatar & Name */}
                <div className="flex gap-4">
                  <div>
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Avatar
                    </label>
                    <div className="flex flex-wrap gap-2 w-48">
                      {AVATAR_OPTIONS.map(avatar => (
                        <button
                          key={avatar}
                          onClick={() => setNewAgentForm(prev => ({ ...prev, avatar }))}
                          className={clsx(
                            'w-10 h-10 rounded-lg text-xl flex items-center justify-center transition-colors',
                            newAgentForm.avatar === avatar
                              ? 'bg-brand-100 dark:bg-brand-900/30 ring-2 ring-brand-500'
                              : 'bg-surface-100 dark:bg-surface-700 hover:bg-surface-200 dark:hover:bg-surface-600'
                          )}
                        >
                          {avatar}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div className="flex-1">
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Name *
                    </label>
                    <input
                      type="text"
                      value={newAgentForm.name}
                      onChange={(e) => setNewAgentForm(prev => ({ ...prev, name: e.target.value }))}
                      placeholder="My AI Agent"
                      className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                    />
                    <div className="mt-3">
                      <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                        Description
                      </label>
                      <textarea
                        value={newAgentForm.description}
                        onChange={(e) => setNewAgentForm(prev => ({ ...prev, description: e.target.value }))}
                        placeholder="What does this agent do?"
                        rows={2}
                        className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                      />
                    </div>
                  </div>
                </div>

                {/* Model */}
                <div>
                  <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                    Model
                  </label>
                  <select
                    value={newAgentForm.modelId}
                    onChange={(e) => setNewAgentForm(prev => ({ ...prev, modelId: e.target.value }))}
                    className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                  >
                    {MODEL_OPTIONS.map(model => (
                      <option key={model.id} value={model.id}>
                        {model.name} - {model.description}
                      </option>
                    ))}
                  </select>
                </div>

                {/* Capabilities */}
                <div>
                  <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">
                    Capabilities
                  </label>
                  <div className="flex flex-wrap gap-2">
                    {(['text', 'vision', 'audio', 'code', 'web', 'files'] as Capability[]).map(cap => {
                      const Icon = CAPABILITY_ICONS[cap];
                      const isSelected = newAgentForm.capabilities.includes(cap);
                      return (
                        <button
                          key={cap}
                          onClick={() => setNewAgentForm(prev => ({
                            ...prev,
                            capabilities: isSelected
                              ? prev.capabilities.filter(c => c !== cap)
                              : [...prev.capabilities, cap],
                          }))}
                          className={clsx(
                            'flex items-center gap-2 px-3 py-2 rounded-lg text-sm transition-colors',
                            isSelected
                              ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300 ring-1 ring-brand-500'
                              : 'bg-surface-100 dark:bg-surface-700 text-surface-600 dark:text-surface-400 hover:bg-surface-200 dark:hover:bg-surface-600'
                          )}
                        >
                          <Icon className="w-4 h-4" />
                          {cap.charAt(0).toUpperCase() + cap.slice(1)}
                        </button>
                      );
                    })}
                  </div>
                </div>

                {/* Tools */}
                <div>
                  <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">
                    Tools
                  </label>
                  <div className="flex flex-wrap gap-2">
                    {AVAILABLE_TOOLS.filter(t => t.enabled).map(tool => {
                      const Icon = TOOL_ICONS[tool.icon] || Wrench;
                      const isSelected = newAgentForm.tools.includes(tool.id);
                      return (
                        <button
                          key={tool.id}
                          onClick={() => setNewAgentForm(prev => ({
                            ...prev,
                            tools: isSelected
                              ? prev.tools.filter(t => t !== tool.id)
                              : [...prev.tools, tool.id],
                          }))}
                          className={clsx(
                            'flex items-center gap-2 px-3 py-2 rounded-lg text-sm transition-colors',
                            isSelected
                              ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300 ring-1 ring-brand-500'
                              : 'bg-surface-100 dark:bg-surface-700 text-surface-600 dark:text-surface-400 hover:bg-surface-200 dark:hover:bg-surface-600'
                          )}
                        >
                          <Icon className="w-4 h-4" />
                          {tool.name}
                        </button>
                      );
                    })}
                  </div>
                </div>

                {/* System Prompt */}
                <div>
                  <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                    System Prompt
                  </label>
                  <textarea
                    value={newAgentForm.systemPrompt}
                    onChange={(e) => setNewAgentForm(prev => ({ ...prev, systemPrompt: e.target.value }))}
                    placeholder="Define the agent's behavior and personality..."
                    rows={4}
                    className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500 font-mono"
                  />
                </div>

                {/* Advanced Settings */}
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Temperature
                    </label>
                    <input
                      type="number"
                      value={newAgentForm.temperature}
                      onChange={(e) => setNewAgentForm(prev => ({ ...prev, temperature: parseFloat(e.target.value) || 0.7 }))}
                      min={0}
                      max={2}
                      step={0.1}
                      className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                    />
                  </div>
                  <div>
                    <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                      Max Tokens
                    </label>
                    <input
                      type="number"
                      value={newAgentForm.maxTokens}
                      onChange={(e) => setNewAgentForm(prev => ({ ...prev, maxTokens: parseInt(e.target.value) || 2048 }))}
                      min={256}
                      max={8192}
                      className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
                    />
                  </div>
                </div>
              </div>
              <div className="p-6 border-t border-surface-200 dark:border-surface-700 flex justify-end gap-3">
                <Button variant="secondary" onClick={() => setShowCreateModal(false)}>
                  Cancel
                </Button>
                <Button variant="primary" onClick={handleCreateAgent} disabled={!newAgentForm.name.trim()}>
                  <Plus className="w-4 h-4 mr-1.5" />
                  Create Agent
                </Button>
              </div>
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Run Agent Modal */}
      <AnimatePresence>
        {showRunModal && runAgent && (
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            className="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
            onClick={() => {
              if (!isRunning) {
                setShowRunModal(false);
                setRunAgent(null);
                setTaskInput('');
              }
            }}
          >
            <motion.div
              initial={{ scale: 0.95, opacity: 0 }}
              animate={{ scale: 1, opacity: 1 }}
              exit={{ scale: 0.95, opacity: 0 }}
              className="bg-white dark:bg-surface-800 rounded-xl shadow-xl w-full max-w-lg m-4"
              onClick={(e) => e.stopPropagation()}
            >
              <div className="p-6 border-b border-surface-200 dark:border-surface-700">
                <div className="flex items-center gap-3">
                  <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center text-2xl">
                    {runAgent.avatar}
                  </div>
                  <div>
                    <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
                      Run {runAgent.name}
                    </h2>
                    <p className="text-sm text-surface-500">
                      Enter a task for this agent to perform
                    </p>
                  </div>
                </div>
              </div>
              <div className="p-6">
                <textarea
                  value={taskInput}
                  onChange={(e) => setTaskInput(e.target.value)}
                  placeholder="Describe the task you want the agent to perform..."
                  rows={4}
                  disabled={isRunning}
                  className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500 disabled:opacity-50"
                />
                {isRunning && (
                  <div className="mt-4 p-4 rounded-lg bg-surface-50 dark:bg-surface-900">
                    <div className="flex items-center gap-2 mb-2">
                      <Loader2 className="w-4 h-4 animate-spin text-brand-500" />
                      <span className="text-sm font-medium text-surface-900 dark:text-white">Running...</span>
                    </div>
                    <p className="text-xs text-surface-500">The agent is processing your task</p>
                  </div>
                )}
              </div>
              <div className="p-6 border-t border-surface-200 dark:border-surface-700 flex justify-end gap-3">
                <Button
                  variant="secondary"
                  onClick={() => {
                    setShowRunModal(false);
                    setRunAgent(null);
                    setTaskInput('');
                  }}
                  disabled={isRunning}
                >
                  Cancel
                </Button>
                <Button
                  variant="primary"
                  onClick={() => handleRunAgent(runAgent, taskInput)}
                  disabled={!taskInput.trim() || isRunning}
                >
                  {isRunning ? (
                    <>
                      <Loader2 className="w-4 h-4 mr-1.5 animate-spin" />
                      Running...
                    </>
                  ) : (
                    <>
                      <Play className="w-4 h-4 mr-1.5" />
                      Run Task
                    </>
                  )}
                </Button>
              </div>
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Agent Detail Modal */}
      <AnimatePresence>
        {selectedAgent && (
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            className="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
            onClick={() => setSelectedAgent(null)}
          >
            <motion.div
              initial={{ scale: 0.95, opacity: 0 }}
              animate={{ scale: 1, opacity: 1 }}
              exit={{ scale: 0.95, opacity: 0 }}
              className="bg-white dark:bg-surface-800 rounded-xl shadow-xl w-full max-w-2xl m-4 max-h-[90vh] overflow-y-auto"
              onClick={(e) => e.stopPropagation()}
            >
              <div className="p-6 border-b border-surface-200 dark:border-surface-700 flex items-center justify-between">
                <div className="flex items-center gap-4">
                  <div className="w-16 h-16 rounded-xl bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center text-3xl">
                    {selectedAgent.avatar}
                  </div>
                  <div>
                    <div className="flex items-center gap-2">
                      <h2 className="text-xl font-semibold text-surface-900 dark:text-white">
                        {selectedAgent.name}
                      </h2>
                      {selectedAgent.isFavorite && <Star className="w-5 h-5 fill-amber-400 text-amber-400" />}
                    </div>
                    <Badge variant={selectedAgent.status === 'active' ? 'success' : selectedAgent.status === 'running' ? 'warning' : 'default'} dot>
                      {selectedAgent.status}
                    </Badge>
                  </div>
                </div>
                <IconButton
                  icon={<X className="w-5 h-5" />}
                  label="Close"
                  onClick={() => setSelectedAgent(null)}
                />
              </div>
              <div className="p-6 space-y-6">
                <p className="text-surface-600 dark:text-surface-400">{selectedAgent.description}</p>

                {/* Stats */}
                <div className="grid grid-cols-4 gap-4">
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900 text-center">
                    <p className="text-2xl font-bold text-surface-900 dark:text-white">{selectedAgent.stats.tasksCompleted}</p>
                    <p className="text-xs text-surface-500">Tasks Completed</p>
                  </div>
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900 text-center">
                    <p className="text-2xl font-bold text-surface-900 dark:text-white">{selectedAgent.stats.avgResponseTime.toFixed(1)}s</p>
                    <p className="text-xs text-surface-500">Avg Response</p>
                  </div>
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900 text-center">
                    <p className="text-2xl font-bold text-surface-900 dark:text-white">{selectedAgent.stats.successRate}%</p>
                    <p className="text-xs text-surface-500">Success Rate</p>
                  </div>
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900 text-center">
                    <p className="text-2xl font-bold text-surface-900 dark:text-white">{(selectedAgent.stats.totalTokens / 1000).toFixed(0)}k</p>
                    <p className="text-xs text-surface-500">Tokens Used</p>
                  </div>
                </div>

                {/* Configuration */}
                <div className="space-y-4">
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">Model</p>
                    <p className="text-surface-900 dark:text-white">{MODEL_OPTIONS.find(m => m.id === selectedAgent.modelId)?.name || selectedAgent.modelId}</p>
                  </div>
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">Capabilities</p>
                    <div className="flex flex-wrap gap-2">
                      {selectedAgent.capabilities.map(cap => {
                        const Icon = CAPABILITY_ICONS[cap];
                        return (
                          <Badge key={cap} variant="default">
                            <Icon className="w-3 h-3 mr-1" />
                            {cap}
                          </Badge>
                        );
                      })}
                    </div>
                  </div>
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">Tools</p>
                    <div className="flex flex-wrap gap-2">
                      {selectedAgent.tools.map(toolId => {
                        const tool = AVAILABLE_TOOLS.find(t => t.id === toolId);
                        if (!tool) return null;
                        const Icon = TOOL_ICONS[tool.icon] || Wrench;
                        return (
                          <Badge key={toolId} variant="default">
                            <Icon className="w-3 h-3 mr-1" />
                            {tool.name}
                          </Badge>
                        );
                      })}
                    </div>
                  </div>
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">System Prompt</p>
                    <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900 font-mono text-sm text-surface-600 dark:text-surface-400">
                      {selectedAgent.systemPrompt || 'No system prompt configured'}
                    </div>
                  </div>
                </div>
              </div>
              <div className="p-6 border-t border-surface-200 dark:border-surface-700 flex justify-between">
                <div className="flex items-center gap-2">
                  <Button variant="secondary" onClick={() => handleDuplicateAgent(selectedAgent)}>
                    <Copy className="w-4 h-4 mr-1.5" />
                    Duplicate
                  </Button>
                  <Button variant="danger" onClick={() => handleDeleteAgent(selectedAgent.id)}>
                    <Trash2 className="w-4 h-4 mr-1.5" />
                    Delete
                  </Button>
                </div>
                <Button
                  variant="primary"
                  onClick={() => {
                    setRunAgent(selectedAgent);
                    setSelectedAgent(null);
                    setShowRunModal(true);
                  }}
                >
                  <Play className="w-4 h-4 mr-1.5" />
                  Run Agent
                </Button>
              </div>
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Run Detail Modal */}
      <AnimatePresence>
        {selectedRun && (
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            className="fixed inset-0 bg-black/50 flex items-center justify-center z-50"
            onClick={() => setSelectedRun(null)}
          >
            <motion.div
              initial={{ scale: 0.95, opacity: 0 }}
              animate={{ scale: 1, opacity: 1 }}
              exit={{ scale: 0.95, opacity: 0 }}
              className="bg-white dark:bg-surface-800 rounded-xl shadow-xl w-full max-w-2xl m-4 max-h-[90vh] overflow-y-auto"
              onClick={(e) => e.stopPropagation()}
            >
              <div className="p-6 border-b border-surface-200 dark:border-surface-700 flex items-center justify-between">
                <div>
                  <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
                    Run Details
                  </h2>
                  <p className="text-sm text-surface-500">{selectedRun.agentName}</p>
                </div>
                <div className="flex items-center gap-2">
                  <Badge variant={selectedRun.status === 'completed' ? 'success' : selectedRun.status === 'failed' ? 'error' : 'warning'}>
                    {selectedRun.status}
                  </Badge>
                  <IconButton
                    icon={<X className="w-5 h-5" />}
                    label="Close"
                    onClick={() => setSelectedRun(null)}
                  />
                </div>
              </div>
              <div className="p-6 space-y-4">
                <div>
                  <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">Task</p>
                  <p className="text-surface-900 dark:text-white">{selectedRun.task}</p>
                </div>

                <div className="grid grid-cols-3 gap-4">
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900">
                    <p className="text-xs text-surface-500 mb-1">Started</p>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {validateDate(selectedRun.startedAt).toLocaleString()}
                    </p>
                  </div>
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900">
                    <p className="text-xs text-surface-500 mb-1">Duration</p>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {selectedRun.durationMs ? formatDuration(selectedRun.durationMs) : '-'}
                    </p>
                  </div>
                  <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-900">
                    <p className="text-xs text-surface-500 mb-1">Tokens</p>
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {selectedRun.tokensUsed?.toLocaleString() || '-'}
                    </p>
                  </div>
                </div>

                {selectedRun.steps.length > 0 && (
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2">Execution Steps</p>
                    <div className="space-y-2">
                      {selectedRun.steps.map((step) => (
                        <div key={step.id} className="flex gap-3 p-3 rounded-lg bg-surface-50 dark:bg-surface-900">
                          <div className={clsx(
                            'w-6 h-6 rounded-full flex items-center justify-center flex-shrink-0',
                            step.type === 'thought' ? 'bg-brand-100 dark:bg-brand-900/30' :
                            step.type === 'tool_call' ? 'bg-amber-100 dark:bg-amber-900/30' :
                            step.type === 'tool_result' ? 'bg-emerald-100 dark:bg-emerald-900/30' :
                            'bg-purple-100 dark:bg-purple-900/30'
                          )}>
                            {step.type === 'thought' && <Brain className="w-3 h-3 text-brand-600" />}
                            {step.type === 'tool_call' && <Wrench className="w-3 h-3 text-amber-600" />}
                            {step.type === 'tool_result' && <CheckCircle2 className="w-3 h-3 text-emerald-600" />}
                            {step.type === 'response' && <MessageSquare className="w-3 h-3 text-purple-600" />}
                          </div>
                          <div className="flex-1 min-w-0">
                            <p className="text-xs text-surface-500 mb-1 capitalize">{step.type.replace('_', ' ')}</p>
                            <p className="text-sm text-surface-900 dark:text-white">{step.content}</p>
                          </div>
                        </div>
                      ))}
                    </div>
                  </div>
                )}

                {selectedRun.result && (
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">Result</p>
                    <div className="p-3 rounded-lg bg-success-50 dark:bg-success-900/20 border border-success-200 dark:border-success-800">
                      <p className="text-sm text-success-700 dark:text-success-300">{selectedRun.result}</p>
                    </div>
                  </div>
                )}

                {selectedRun.error && (
                  <div>
                    <p className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">Error</p>
                    <div className="p-3 rounded-lg bg-error-50 dark:bg-error-900/20 border border-error-200 dark:border-error-800">
                      <p className="text-sm text-error-700 dark:text-error-300">{selectedRun.error}</p>
                    </div>
                  </div>
                )}
              </div>
            </motion.div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}
