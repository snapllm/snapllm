// ============================================================================
// SnapLLM Enterprise - A/B Model Compare
// Advanced Side-by-Side Model Comparison with Analytics, ELO Ratings & Charts
// ============================================================================

import React, { useState, useCallback, useMemo, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  RadarChart,
  PolarGrid,
  PolarAngleAxis,
  PolarRadiusAxis,
  Radar,
  Legend,
  LineChart,
  Line,
  PieChart,
  Pie,
  Cell,
} from 'recharts';
import {
  GitCompare,
  Send,
  Loader2,
  Copy,
  Check,
  Trash2,
  Plus,
  Clock,
  Zap,
  Award,
  ThumbsUp,
  ThumbsDown,
  BarChart3,
  ChevronDown,
  ChevronUp,
  Settings2,
  History,
  Star,
  Download,
  Share2,
  RefreshCw,
  CheckCircle2,
  XCircle,
  Cpu,
  Brain,
  Target,
  Sparkles,
  Scale,
  ArrowLeftRight,
  Timer,
  TrendingUp,
  TrendingDown,
  Medal,
  Trophy,
  Crown,
  Activity,
  LayoutGrid,
  List,
  PieChart as PieChartIcon,
  FileText,
  Gauge,
  Hash,
  MoreVertical,
  Eye,
  Filter,
  Calendar,
  Layers,
  Database,
} from 'lucide-react';
import { listModels, sendChatMessage } from '../lib/api';
import { filterChainOfThought } from '../utils/chainOfThought';
import { useModelStore } from '../store';
import { Button, IconButton, Badge, Card, Progress, Toggle, Modal } from '../components/ui';
import { MarkdownRenderer } from '../components/ui/MarkdownRenderer';

// ============================================================================
// Types
// ============================================================================

interface ComparisonResult {
  id: string;
  modelId: string;
  modelName: string;
  response: string;
  loading: boolean;
  error?: string;
  latencyMs: number;
  tokensPerSecond?: number;
  totalTokens?: number;
  characterCount?: number;
  wordCount?: number;
  sentenceCount?: number;
  vote?: 'winner' | 'loser' | null;
  // vPID L2 Context metrics
  cacheHit?: boolean;
  contextTokens?: number;
  speedup?: string;
}

interface ComparisonRound {
  id: string;
  prompt: string;
  results: ComparisonResult[];
  timestamp: Date;
  winnerId?: string;
  category?: string;
}

interface ModelStats {
  modelId: string;
  modelName: string;
  wins: number;
  losses: number;
  total: number;
  winRate: number;
  avgLatency: number;
  avgTokensPerSec: number;
  avgResponseLength: number;
  eloRating: number;
}

interface LeaderboardEntry {
  rank: number;
  modelId: string;
  modelName: string;
  eloRating: number;
  wins: number;
  losses: number;
  winRate: number;
  trend: 'up' | 'down' | 'stable';
  lastChange: number;
}

// ============================================================================
// Constants
// ============================================================================

const PROMPT_CATEGORIES = [
  { id: 'general', label: 'General Knowledge', icon: Brain },
  { id: 'coding', label: 'Coding', icon: FileText },
  { id: 'creative', label: 'Creative Writing', icon: Sparkles },
  { id: 'reasoning', label: 'Reasoning', icon: Target },
  { id: 'analysis', label: 'Analysis', icon: Activity },
];

const QUICK_PROMPTS = [
  { text: 'Explain quantum computing in simple terms', category: 'general' },
  { text: 'Write a haiku about programming', category: 'creative' },
  { text: 'What are the benefits of TypeScript?', category: 'coding' },
  { text: 'How does machine learning work?', category: 'general' },
  { text: 'Write a function to reverse a linked list', category: 'coding' },
  { text: 'Analyze the pros and cons of remote work', category: 'analysis' },
  { text: 'Solve: If 3x + 5 = 20, what is x?', category: 'reasoning' },
  { text: 'Write a short story opening about a robot', category: 'creative' },
];

const CHART_COLORS = ['#0ea5e9', '#ec4899', '#10b981', '#8b5cf6', '#f59e0b', '#06b6d4'];

const BASE_ELO = 1500;
const K_FACTOR = 32;

// ============================================================================
// ELO Rating Calculator
// ============================================================================

function calculateEloChange(winnerRating: number, loserRating: number): { winnerGain: number; loserLoss: number } {
  const expectedWinner = 1 / (1 + Math.pow(10, (loserRating - winnerRating) / 400));
  const expectedLoser = 1 / (1 + Math.pow(10, (winnerRating - loserRating) / 400));

  const winnerGain = Math.round(K_FACTOR * (1 - expectedWinner));
  const loserLoss = Math.round(K_FACTOR * (0 - expectedLoser));

  return { winnerGain, loserLoss: Math.abs(loserLoss) };
}

// ============================================================================
// Text Analysis Utilities
// ============================================================================

function analyzeResponse(text: string): { characters: number; words: number; sentences: number } {
  const characters = text.length;
  const words = text.split(/\s+/).filter(w => w.length > 0).length;
  const sentences = text.split(/[.!?]+/).filter(s => s.trim().length > 0).length;
  return { characters, words, sentences };
}

// ============================================================================
// Custom Tooltip Component
// ============================================================================

const CustomTooltip = ({ active, payload, label }: any) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 rounded-lg shadow-lg p-3">
        <p className="text-sm font-semibold text-surface-900 dark:text-white mb-2">{label}</p>
        {payload.map((entry: any, index: number) => (
          <p key={index} className="text-xs" style={{ color: entry.color }}>
            {entry.name}: {typeof entry.value === 'number' ? entry.value.toFixed(1) : entry.value}
          </p>
        ))}
      </div>
    );
  }
  return null;
};

// ============================================================================
// Main Component
// ============================================================================

export default function Compare() {
  const { models: storeModels } = useModelStore();

  // State
  const [prompt, setPrompt] = useState('');
  const [selectedCategory, setSelectedCategory] = useState<string>('general');
  const [selectedModels, setSelectedModels] = useState<string[]>([]);
  const [currentResults, setCurrentResults] = useState<ComparisonResult[]>([]);
  const [comparisonHistory, setComparisonHistory] = useState<ComparisonRound[]>(() => {
    // Load from localStorage
    const saved = localStorage.getItem('snapllm_comparison_history');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        return parsed.map((r: any) => ({ ...r, timestamp: new Date(r.timestamp) }));
      } catch { return []; }
    }
    return [];
  });
  const [modelEloRatings, setModelEloRatings] = useState<Record<string, number>>(() => {
    const saved = localStorage.getItem('snapllm_model_elo');
    return saved ? JSON.parse(saved) : {};
  });
  const [shouldFilterCoT, setShouldFilterCoT] = useState(true);
  const [showHistory, setShowHistory] = useState(false);
  const [showAnalytics, setShowAnalytics] = useState(false);
  const [showLeaderboard, setShowLeaderboard] = useState(false);
  const [copiedId, setCopiedId] = useState<string | null>(null);
  const [isComparing, setIsComparing] = useState(false);
  const [viewMode, setViewMode] = useState<'grid' | 'list'>('grid');
  const [activeTab, setActiveTab] = useState<'compare' | 'analytics' | 'leaderboard'>('compare');

  // Persist history and ELO to localStorage
  useEffect(() => {
    localStorage.setItem('snapllm_comparison_history', JSON.stringify(comparisonHistory));
  }, [comparisonHistory]);

  useEffect(() => {
    localStorage.setItem('snapllm_model_elo', JSON.stringify(modelEloRatings));
  }, [modelEloRatings]);

  // Fetch models
  const { data: modelsResponse, isLoading: modelsLoading } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
  });

  // Only show LLM models - diffusion/vision models can't be compared via chat completions
  const models = modelsResponse?.models?.filter((m: any) =>
    m.status === 'loaded' && (m.type === 'llm' || !m.type)
  ) || [];

  // Calculate comprehensive model stats
  const modelStats = useMemo((): ModelStats[] => {
    const statsMap = new Map<string, ModelStats>();

    comparisonHistory.forEach(round => {
      round.results.forEach(result => {
        if (!statsMap.has(result.modelId)) {
          statsMap.set(result.modelId, {
            modelId: result.modelId,
            modelName: result.modelName,
            wins: 0,
            losses: 0,
            total: 0,
            winRate: 0,
            avgLatency: 0,
            avgTokensPerSec: 0,
            avgResponseLength: 0,
            eloRating: modelEloRatings[result.modelId] || BASE_ELO,
          });
        }

        const stats = statsMap.get(result.modelId)!;
        stats.total++;
        if (result.vote === 'winner') stats.wins++;
        if (result.vote === 'loser') stats.losses++;
        stats.avgLatency = ((stats.avgLatency * (stats.total - 1)) + (result.latencyMs || 0)) / stats.total;
        stats.avgTokensPerSec = ((stats.avgTokensPerSec * (stats.total - 1)) + (result.tokensPerSecond || 0)) / stats.total;
        stats.avgResponseLength = ((stats.avgResponseLength * (stats.total - 1)) + (result.characterCount || 0)) / stats.total;
      });
    });

    return Array.from(statsMap.values()).map(s => ({
      ...s,
      winRate: s.total > 0 ? (s.wins / s.total) * 100 : 0,
      eloRating: modelEloRatings[s.modelId] || BASE_ELO,
    }));
  }, [comparisonHistory, modelEloRatings]);

  // Build leaderboard
  const leaderboard = useMemo((): LeaderboardEntry[] => {
    return modelStats
      .sort((a, b) => b.eloRating - a.eloRating)
      .map((stats, index) => ({
        rank: index + 1,
        modelId: stats.modelId,
        modelName: stats.modelName,
        eloRating: stats.eloRating,
        wins: stats.wins,
        losses: stats.losses,
        winRate: stats.winRate,
        trend: stats.eloRating > BASE_ELO ? 'up' : stats.eloRating < BASE_ELO ? 'down' : 'stable',
        lastChange: stats.eloRating - BASE_ELO,
      }));
  }, [modelStats]);

  // Chart data for analytics
  const performanceChartData = useMemo(() => {
    return modelStats.map(s => ({
      name: s.modelName.slice(0, 10),
      latency: s.avgLatency,
      tokensPerSec: s.avgTokensPerSec,
      responseLength: s.avgResponseLength / 100,
    }));
  }, [modelStats]);

  const winRateChartData = useMemo(() => {
    return modelStats.map((s, i) => ({
      name: s.modelName.slice(0, 10),
      wins: s.wins,
      losses: s.losses,
      winRate: s.winRate,
      fill: CHART_COLORS[i % CHART_COLORS.length],
    }));
  }, [modelStats]);

  const categoryDistribution = useMemo(() => {
    const counts: Record<string, number> = {};
    comparisonHistory.forEach(r => {
      const cat = r.category || 'general';
      counts[cat] = (counts[cat] || 0) + 1;
    });
    return Object.entries(counts).map(([name, value], i) => ({
      name: PROMPT_CATEGORIES.find(c => c.id === name)?.label || name,
      value,
      fill: CHART_COLORS[i % CHART_COLORS.length],
    }));
  }, [comparisonHistory]);

  const radarData = useMemo(() => {
    if (selectedModels.length < 2) return [];
    return ['Speed', 'Accuracy', 'Length', 'Quality', 'Consistency'].map(metric => {
      const dataPoint: any = { metric };
      selectedModels.forEach((modelId, i) => {
        const stats = modelStats.find(s => s.modelId === modelId);
        if (stats) {
          switch (metric) {
            case 'Speed':
              dataPoint[modelId] = Math.min(100, stats.avgTokensPerSec * 2);
              break;
            case 'Accuracy':
              dataPoint[modelId] = stats.winRate;
              break;
            case 'Length':
              dataPoint[modelId] = Math.min(100, stats.avgResponseLength / 10);
              break;
            case 'Quality':
              dataPoint[modelId] = stats.eloRating / 20;
              break;
            case 'Consistency':
              dataPoint[modelId] = stats.total > 0 ? 50 + (stats.winRate / 2) : 50;
              break;
          }
        }
      });
      return dataPoint;
    });
  }, [selectedModels, modelStats]);

  // Toggle model selection
  const handleModelToggle = (modelId: string) => {
    if (selectedModels.includes(modelId)) {
      setSelectedModels(selectedModels.filter(id => id !== modelId));
    } else if (selectedModels.length < 4) {
      setSelectedModels([...selectedModels, modelId]);
    }
  };

  // Run comparison
  const handleCompare = async () => {
    if (!prompt.trim() || selectedModels.length < 2) return;

    setIsComparing(true);

    // Initialize results
    const initialResults: ComparisonResult[] = selectedModels.map(modelId => {
      const model = models.find((m: any) => m.id === modelId);
      return {
        id: crypto.randomUUID(),
        modelId,
        modelName: model?.name || modelId,
        response: '',
        loading: true,
        latencyMs: 0,
      };
    });
    setCurrentResults(initialResults);

    // Send requests to all models in parallel
    const promises = selectedModels.map(async (modelId, index) => {
      const startTime = Date.now();
      try {
        const data = await sendChatMessage({
          model: modelId,
          messages: [{ role: 'user', content: prompt }],
          max_tokens: 1024,
          temperature: 0.7,
          top_p: 0.95,
          repeat_penalty: 1.1,
          stop: ['**END OF RESPONSE**', '</think>', '\n\n\n\n', 'User:', '<|end|>', '</s>'],
        });

        const latencyMs = Date.now() - startTime;
        let response = data.response || data.choices?.[0]?.message?.content || 'No response';

        if (shouldFilterCoT && response !== 'No response') {
          response = filterChainOfThought(response);
        }

        const analysis = analyzeResponse(response);

        setCurrentResults(prev =>
          prev.map((r, i) =>
            i === index
              ? {
                  ...r,
                  response,
                  loading: false,
                  latencyMs,
                  tokensPerSecond: data.usage?.tokens_per_second,
                  totalTokens: data.usage?.total_tokens,
                  characterCount: analysis.characters,
                  wordCount: analysis.words,
                  sentenceCount: analysis.sentences,
                  // vPID L2 Context metrics
                  cacheHit: data.cache_hit,
                  contextTokens: data.usage?.context_tokens,
                  speedup: data.speedup,
                }
              : r
          )
        );
      } catch (error: any) {
        setCurrentResults(prev =>
          prev.map((r, i) =>
            i === index
              ? {
                  ...r,
                  loading: false,
                  error: error.response?.data?.error?.message || error.message || 'Unknown error',
                  latencyMs: Date.now() - startTime,
                }
              : r
          )
        );
      }
    });

    await Promise.all(promises);
    setIsComparing(false);
  };

  // Vote for a response with ELO update
  const voteForResponse = (resultId: string) => {
    const winner = currentResults.find(r => r.id === resultId);
    const losers = currentResults.filter(r => r.id !== resultId && !r.error);

    if (!winner) return;

    // Update ELO ratings
    const newRatings = { ...modelEloRatings };
    const winnerCurrentElo = newRatings[winner.modelId] || BASE_ELO;

    losers.forEach(loser => {
      const loserCurrentElo = newRatings[loser.modelId] || BASE_ELO;
      const { winnerGain, loserLoss } = calculateEloChange(winnerCurrentElo, loserCurrentElo);

      newRatings[winner.modelId] = (newRatings[winner.modelId] || BASE_ELO) + winnerGain;
      newRatings[loser.modelId] = (newRatings[loser.modelId] || BASE_ELO) - loserLoss;
    });

    setModelEloRatings(newRatings);

    // Update results with votes
    setCurrentResults(prev =>
      prev.map(r => ({
        ...r,
        vote: r.id === resultId ? 'winner' : (r.error ? null : 'loser'),
      }))
    );

    // Save to history
    const round: ComparisonRound = {
      id: crypto.randomUUID(),
      prompt,
      results: currentResults.map(r => ({
        ...r,
        vote: r.id === resultId ? 'winner' : (r.error ? null : 'loser'),
      })),
      timestamp: new Date(),
      winnerId: resultId,
      category: selectedCategory,
    };
    setComparisonHistory(prev => [round, ...prev]);
  };

  // Copy response
  const copyResponse = (result: ComparisonResult) => {
    navigator.clipboard.writeText(result.response);
    setCopiedId(result.id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  // Clear results
  const clearResults = () => {
    setCurrentResults([]);
    setPrompt('');
  };

  // Export data
  const exportData = () => {
    const data = {
      comparisons: comparisonHistory,
      eloRatings: modelEloRatings,
      leaderboard,
      exportedAt: new Date().toISOString(),
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `snapllm-compare-data-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  // Get rank badge
  const getRankBadge = (rank: number) => {
    if (rank === 1) return <Crown className="w-5 h-5 text-yellow-500" />;
    if (rank === 2) return <Medal className="w-5 h-5 text-gray-400" />;
    if (rank === 3) return <Medal className="w-5 h-5 text-amber-600" />;
    return <span className="text-sm font-bold text-surface-500">#{rank}</span>;
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-orange-500 to-amber-600 flex items-center justify-center">
            <GitCompare className="w-6 h-6 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
              A/B Model Compare
            </h1>
            <p className="text-surface-500">
              Compare model outputs with ELO ratings and analytics
            </p>
          </div>
        </div>

        <div className="flex items-center gap-3">
          <Button variant="secondary" onClick={exportData}>
            <Download className="w-4 h-4" />
            Export
          </Button>
          <label className="flex items-center gap-2 cursor-pointer px-3 py-2 rounded-lg bg-surface-100 dark:bg-surface-800">
            <span className="text-sm text-surface-600 dark:text-surface-400">Filter CoT</span>
            <Toggle checked={shouldFilterCoT} onChange={setShouldFilterCoT} />
          </label>
        </div>
      </div>

      {/* Tab Navigation */}
      <div className="flex items-center gap-1 p-1 bg-surface-100 dark:bg-surface-800 rounded-xl w-fit">
        {[
          { id: 'compare', label: 'Compare', icon: Scale },
          { id: 'analytics', label: 'Analytics', icon: BarChart3 },
          { id: 'leaderboard', label: 'Leaderboard', icon: Trophy },
        ].map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id as any)}
            className={clsx(
              'flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium transition-all',
              activeTab === tab.id
                ? 'bg-white dark:bg-surface-700 text-brand-600 dark:text-brand-400 shadow-sm'
                : 'text-surface-600 dark:text-surface-400 hover:text-surface-900 dark:hover:text-white'
            )}
          >
            <tab.icon className="w-4 h-4" />
            {tab.label}
          </button>
        ))}
      </div>

      {/* Compare Tab */}
      {activeTab === 'compare' && (
        <div className="space-y-6">
          {/* Stats Cards */}
          <div className="grid grid-cols-4 gap-4">
            <Card className="p-4">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
                  <GitCompare className="w-5 h-5 text-brand-600 dark:text-brand-400" />
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {comparisonHistory.length}
                  </p>
                  <p className="text-sm text-surface-500">Total Comparisons</p>
                </div>
              </div>
            </Card>
            <Card className="p-4">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
                  <Cpu className="w-5 h-5 text-success-600 dark:text-success-400" />
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {modelStats.length}
                  </p>
                  <p className="text-sm text-surface-500">Models Tested</p>
                </div>
              </div>
            </Card>
            <Card className="p-4">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
                  <Trophy className="w-5 h-5 text-ai-purple" />
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {leaderboard[0]?.modelName?.slice(0, 12) || 'N/A'}
                  </p>
                  <p className="text-sm text-surface-500">Top Performer</p>
                </div>
              </div>
            </Card>
            <Card className="p-4">
              <div className="flex items-center gap-3">
                <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
                  <Gauge className="w-5 h-5 text-warning-600 dark:text-warning-400" />
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {leaderboard[0]?.eloRating || BASE_ELO}
                  </p>
                  <p className="text-sm text-surface-500">Top ELO Rating</p>
                </div>
              </div>
            </Card>
          </div>

          {/* Model Selection */}
          <Card className="p-4">
            <div className="flex items-center justify-between mb-4">
              <h3 className="text-sm font-semibold text-surface-700 dark:text-surface-300">
                Select Models to Compare (2-4)
              </h3>
              <Badge variant="brand">{selectedModels.length}/4 selected</Badge>
            </div>
            <div className="flex flex-wrap gap-2">
              {models.length > 0 ? (
                models.map((model: any) => {
                  const isSelected = selectedModels.includes(model.id);
                  const stats = modelStats.find(s => s.modelId === model.id);
                  const elo = modelEloRatings[model.id] || BASE_ELO;
                  return (
                    <button
                      key={model.id}
                      onClick={() => handleModelToggle(model.id)}
                      disabled={!isSelected && selectedModels.length >= 4}
                      className={clsx(
                        'relative flex items-center gap-2 px-4 py-2.5 rounded-xl border-2 transition-all disabled:opacity-50',
                        isSelected
                          ? 'border-brand-500 bg-brand-50 dark:bg-brand-900/20'
                          : 'border-surface-200 dark:border-surface-700 hover:border-surface-300 dark:hover:border-surface-600'
                      )}
                    >
                      <Cpu className={clsx('w-4 h-4', isSelected ? 'text-brand-600' : 'text-surface-400')} />
                      <div className="text-left">
                        <span className={clsx(
                          'text-sm font-medium block',
                          isSelected ? 'text-brand-700 dark:text-brand-300' : 'text-surface-700 dark:text-surface-300'
                        )}>
                          {model.name}
                        </span>
                        <span className="text-xs text-surface-500">
                          ELO: {elo} {stats && `• ${stats.wins}W`}
                        </span>
                      </div>
                      {isSelected && (
                        <div className="absolute -top-1.5 -right-1.5 w-5 h-5 rounded-full bg-brand-500 flex items-center justify-center">
                          <Check className="w-3 h-3 text-white" />
                        </div>
                      )}
                    </button>
                  );
                })
              ) : (
                <p className="text-sm text-surface-500 py-4">
                  {modelsLoading ? 'Loading models...' : 'No models loaded. Load models from the Model Hub.'}
                </p>
              )}
            </div>
          </Card>

          {/* Prompt Input with Category */}
          <Card className="p-4">
            <div className="flex items-center gap-2 mb-4">
              {PROMPT_CATEGORIES.map(cat => (
                <button
                  key={cat.id}
                  onClick={() => setSelectedCategory(cat.id)}
                  className={clsx(
                    'flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-medium transition-all',
                    selectedCategory === cat.id
                      ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300'
                      : 'text-surface-500 hover:bg-surface-100 dark:hover:bg-surface-800'
                  )}
                >
                  <cat.icon className="w-3.5 h-3.5" />
                  {cat.label}
                </button>
              ))}
            </div>
            <div className="flex gap-3">
              <textarea
                value={prompt}
                onChange={(e) => setPrompt(e.target.value)}
                placeholder="Enter a prompt to compare how different models respond..."
                disabled={selectedModels.length < 2}
                rows={3}
                className="flex-1 resize-none rounded-xl border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-4 py-3 text-[15px] placeholder:text-surface-400 focus:outline-none focus:ring-2 focus:ring-brand-500 focus:border-transparent disabled:opacity-50"
              />
              <div className="flex flex-col gap-2">
                <Button
                  variant="gradient"
                  size="lg"
                  onClick={handleCompare}
                  disabled={!prompt.trim() || selectedModels.length < 2 || isComparing}
                  className="h-full"
                >
                  {isComparing ? (
                    <Loader2 className="w-5 h-5 animate-spin" />
                  ) : (
                    <>
                      <ArrowLeftRight className="w-5 h-5" />
                      Compare
                    </>
                  )}
                </Button>
                {currentResults.length > 0 && (
                  <Button variant="ghost" size="sm" onClick={clearResults}>
                    <Trash2 className="w-4 h-4" />
                    Clear
                  </Button>
                )}
              </div>
            </div>

            {/* Quick Prompts */}
            {!prompt && (
              <div className="mt-4 flex flex-wrap gap-2">
                {QUICK_PROMPTS.filter(p => p.category === selectedCategory).map((p) => (
                  <button
                    key={p.text}
                    onClick={() => setPrompt(p.text)}
                    className="px-3 py-1.5 rounded-lg bg-surface-100 dark:bg-surface-800 hover:bg-surface-200 dark:hover:bg-surface-700 text-sm text-surface-600 dark:text-surface-400 transition-colors"
                  >
                    {p.text.slice(0, 40)}...
                  </button>
                ))}
              </div>
            )}
          </Card>

          {/* Results Grid */}
          {currentResults.length > 0 ? (
            <div className={clsx(
              'grid gap-4',
              currentResults.length === 2 ? 'grid-cols-2' :
              currentResults.length === 3 ? 'grid-cols-3' :
              'grid-cols-2 lg:grid-cols-4'
            )}>
              {currentResults.map((result, index) => (
                <motion.div
                  key={result.id}
                  initial={{ opacity: 0, y: 20 }}
                  animate={{ opacity: 1, y: 0 }}
                  transition={{ delay: index * 0.1 }}
                >
                  <Card className={clsx(
                    'flex flex-col h-full',
                    result.vote === 'winner' && 'ring-2 ring-success-500',
                    result.vote === 'loser' && 'opacity-60'
                  )}>
                    {/* Header */}
                    <div className="flex items-center justify-between p-4 border-b border-surface-200 dark:border-surface-700">
                      <div className="flex items-center gap-2">
                        <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center">
                          <Brain className="w-4 h-4 text-brand-600 dark:text-brand-400" />
                        </div>
                        <div>
                          <p className="text-sm font-semibold text-surface-900 dark:text-white">
                            {result.modelName}
                          </p>
                          <p className="text-xs text-surface-500">
                            ELO: {modelEloRatings[result.modelId] || BASE_ELO}
                          </p>
                        </div>
                      </div>
                      {result.vote === 'winner' && (
                        <Badge variant="success">
                          <Award className="w-3 h-3" />
                          Winner
                        </Badge>
                      )}
                    </div>

                    {/* Metrics Bar */}
                    {!result.loading && !result.error && (
                      <div className="flex items-center justify-between flex-wrap gap-2 px-4 py-2 bg-surface-50 dark:bg-surface-800/50 text-xs text-surface-500">
                        <span className="flex items-center gap-1">
                          <Timer className="w-3 h-3" />
                          {result.latencyMs}ms
                        </span>
                        {result.tokensPerSecond && (
                          <span className="flex items-center gap-1">
                            <Zap className="w-3 h-3" />
                            {result.tokensPerSecond.toFixed(1)} tok/s
                          </span>
                        )}
                        <span className="flex items-center gap-1">
                          <Hash className="w-3 h-3" />
                          {result.wordCount || 0} words
                        </span>
                        {/* vPID L2 Cache Hit Indicator */}
                        {result.cacheHit !== undefined && (
                          <span className={clsx(
                            'flex items-center gap-1 px-1.5 py-0.5 rounded-full font-medium',
                            result.cacheHit
                              ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
                              : 'bg-gray-100 dark:bg-gray-800 text-gray-500'
                          )}>
                            <Layers className="w-3 h-3" />
                            {result.cacheHit ? 'L2 Hit' : 'Miss'}
                          </span>
                        )}
                        {result.speedup && (
                          <span className="flex items-center gap-1 text-green-600 dark:text-green-400 font-medium">
                            <Zap className="w-3 h-3" />
                            {result.speedup}
                          </span>
                        )}
                      </div>
                    )}

                    {/* Content */}
                    <div className="flex-1 p-4 min-h-[200px] max-h-[400px] overflow-y-auto">
                      {result.loading ? (
                        <div className="h-full flex flex-col items-center justify-center">
                          <Loader2 className="w-8 h-8 animate-spin text-brand-500 mb-3" />
                          <p className="text-sm text-surface-500">Generating response...</p>
                        </div>
                      ) : result.error ? (
                        <div className="bg-error-50 dark:bg-error-900/20 rounded-lg p-4">
                          <div className="flex items-start gap-2">
                            <XCircle className="w-5 h-5 text-error-500 flex-shrink-0" />
                            <p className="text-sm text-error-700 dark:text-error-300">{result.error}</p>
                          </div>
                        </div>
                      ) : (
                        <MarkdownRenderer
                          content={result.response}
                          className="text-sm"
                        />
                      )}
                    </div>

                    {/* Actions */}
                    {!result.loading && !result.error && (
                      <div className="flex items-center justify-between p-4 border-t border-surface-200 dark:border-surface-700">
                        <div className="flex items-center gap-1">
                          <IconButton
                            icon={copiedId === result.id ? <Check className="w-4 h-4 text-success-500" /> : <Copy className="w-4 h-4" />}
                            label="Copy"
                            onClick={() => copyResponse(result)}
                          />
                        </div>
                        {!result.vote && (
                          <Button
                            variant="success"
                            size="sm"
                            onClick={() => voteForResponse(result.id)}
                          >
                            <ThumbsUp className="w-4 h-4" />
                            Best
                          </Button>
                        )}
                      </div>
                    )}
                  </Card>
                </motion.div>
              ))}
            </div>
          ) : (
            // Empty State
            <Card className="p-12 text-center">
              <div className="w-20 h-20 rounded-2xl bg-gradient-to-br from-orange-500/20 to-amber-600/20 flex items-center justify-center mx-auto mb-6">
                <Scale className="w-10 h-10 text-orange-500" />
              </div>
              <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
                Compare Model Outputs
              </h2>
              <p className="text-surface-500 max-w-md mx-auto">
                Select 2-4 models and enter a prompt to see how different models respond.
                Vote for the best response to update ELO ratings and track performance.
              </p>
            </Card>
          )}

          {/* Recent History */}
          {comparisonHistory.length > 0 && (
            <Card className="p-4">
              <div className="flex items-center justify-between mb-4">
                <h3 className="text-sm font-semibold text-surface-700 dark:text-surface-300 flex items-center gap-2">
                  <History className="w-4 h-4" />
                  Recent Comparisons
                </h3>
                <Button variant="ghost" size="sm" onClick={() => setActiveTab('analytics')}>
                  View All
                  <ChevronDown className="w-4 h-4" />
                </Button>
              </div>
              <div className="grid grid-cols-3 gap-3">
                {comparisonHistory.slice(0, 3).map(round => {
                  const winner = round.results.find(r => r.vote === 'winner');
                  return (
                    <div
                      key={round.id}
                      className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800 hover:bg-surface-100 dark:hover:bg-surface-700 cursor-pointer transition-colors"
                    >
                      <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-2 mb-2">
                        {round.prompt}
                      </p>
                      <div className="flex items-center justify-between">
                        <div className="flex items-center gap-1">
                          <Award className="w-3 h-3 text-success-500" />
                          <span className="text-xs text-success-600 dark:text-success-400">
                            {winner?.modelName?.slice(0, 15)}
                          </span>
                        </div>
                        <span className="text-xs text-surface-500">
                          {round.timestamp.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                        </span>
                      </div>
                    </div>
                  );
                })}
              </div>
            </Card>
          )}
        </div>
      )}

      {/* Analytics Tab */}
      {activeTab === 'analytics' && (
        <div className="space-y-6">
          {modelStats.length > 0 ? (
            <>
              {/* Performance Charts */}
              <div className="grid grid-cols-2 gap-6">
                {/* Win Rate Bar Chart */}
                <Card className="p-6">
                  <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                    <BarChart3 className="w-5 h-5 text-brand-500" />
                    Win/Loss Distribution
                  </h3>
                  <div className="h-64">
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart data={winRateChartData} layout="vertical">
                        <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
                        <XAxis type="number" tick={{ fontSize: 12 }} />
                        <YAxis dataKey="name" type="category" width={80} tick={{ fontSize: 12 }} />
                        <Tooltip content={<CustomTooltip />} />
                        <Bar dataKey="wins" name="Wins" fill="#10b981" stackId="stack" />
                        <Bar dataKey="losses" name="Losses" fill="#ef4444" stackId="stack" />
                      </BarChart>
                    </ResponsiveContainer>
                  </div>
                </Card>

                {/* Performance Comparison */}
                <Card className="p-6">
                  <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                    <Activity className="w-5 h-5 text-ai-purple" />
                    Performance Metrics
                  </h3>
                  <div className="h-64">
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart data={performanceChartData}>
                        <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
                        <XAxis dataKey="name" tick={{ fontSize: 12 }} />
                        <YAxis tick={{ fontSize: 12 }} />
                        <Tooltip content={<CustomTooltip />} />
                        <Legend />
                        <Bar dataKey="latency" name="Latency (ms)" fill="#f59e0b" />
                        <Bar dataKey="tokensPerSec" name="Tokens/sec" fill="#0ea5e9" />
                      </BarChart>
                    </ResponsiveContainer>
                  </div>
                </Card>

                {/* Category Distribution */}
                <Card className="p-6">
                  <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                    <PieChartIcon className="w-5 h-5 text-success-500" />
                    Comparison Categories
                  </h3>
                  <div className="h-64">
                    <ResponsiveContainer width="100%" height="100%">
                      <PieChart>
                        <Pie
                          data={categoryDistribution}
                          cx="50%"
                          cy="50%"
                          outerRadius={80}
                          dataKey="value"
                          label={({ name, percent }) => `${name} (${(percent * 100).toFixed(0)}%)`}
                          labelLine={false}
                        >
                          {categoryDistribution.map((entry, index) => (
                            <Cell key={`cell-${index}`} fill={entry.fill} />
                          ))}
                        </Pie>
                        <Tooltip />
                      </PieChart>
                    </ResponsiveContainer>
                  </div>
                </Card>

                {/* Radar Chart for Selected Models */}
                {selectedModels.length >= 2 && radarData.length > 0 && (
                  <Card className="p-6">
                    <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                      <Target className="w-5 h-5 text-warning-500" />
                      Model Comparison Radar
                    </h3>
                    <div className="h-64">
                      <ResponsiveContainer width="100%" height="100%">
                        <RadarChart data={radarData}>
                          <PolarGrid stroke="#e5e7eb" />
                          <PolarAngleAxis dataKey="metric" tick={{ fontSize: 11 }} />
                          <PolarRadiusAxis angle={30} domain={[0, 100]} tick={{ fontSize: 10 }} />
                          {selectedModels.map((modelId, i) => (
                            <Radar
                              key={modelId}
                              name={models.find((m: any) => m.id === modelId)?.name || modelId}
                              dataKey={modelId}
                              stroke={CHART_COLORS[i % CHART_COLORS.length]}
                              fill={CHART_COLORS[i % CHART_COLORS.length]}
                              fillOpacity={0.2}
                            />
                          ))}
                          <Legend />
                        </RadarChart>
                      </ResponsiveContainer>
                    </div>
                  </Card>
                )}
              </div>

              {/* Detailed Stats Table */}
              <Card className="p-6">
                <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
                  Detailed Model Statistics
                </h3>
                <div className="overflow-x-auto">
                  <table className="w-full text-sm">
                    <thead>
                      <tr className="border-b border-surface-200 dark:border-surface-700">
                        <th className="text-left py-3 px-4 font-semibold text-surface-500">Model</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">ELO</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Wins</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Losses</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Win Rate</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Avg Latency</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Avg Tok/s</th>
                      </tr>
                    </thead>
                    <tbody>
                      {modelStats.map((stats, i) => (
                        <tr key={stats.modelId} className="border-b border-surface-100 dark:border-surface-800">
                          <td className="py-3 px-4 font-medium text-surface-900 dark:text-white">
                            {stats.modelName}
                          </td>
                          <td className="text-center py-3 px-4">
                            <Badge variant={stats.eloRating > BASE_ELO ? 'success' : stats.eloRating < BASE_ELO ? 'error' : 'default'}>
                              {stats.eloRating}
                            </Badge>
                          </td>
                          <td className="text-center py-3 px-4 text-success-600 dark:text-success-400 font-medium">
                            {stats.wins}
                          </td>
                          <td className="text-center py-3 px-4 text-error-600 dark:text-error-400 font-medium">
                            {stats.losses}
                          </td>
                          <td className="text-center py-3 px-4">
                            <div className="flex items-center justify-center gap-2">
                              <Progress value={stats.winRate} variant={stats.winRate > 50 ? 'success' : 'default'} className="w-16" />
                              <span className="text-surface-600 dark:text-surface-400">{stats.winRate.toFixed(0)}%</span>
                            </div>
                          </td>
                          <td className="text-center py-3 px-4 text-surface-600 dark:text-surface-400">
                            {stats.avgLatency.toFixed(0)}ms
                          </td>
                          <td className="text-center py-3 px-4 text-surface-600 dark:text-surface-400">
                            {stats.avgTokensPerSec.toFixed(1)}
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </Card>

              {/* Comparison History */}
              <Card className="p-6">
                <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <History className="w-5 h-5" />
                  Full Comparison History ({comparisonHistory.length})
                </h3>
                <div className="space-y-2 max-h-96 overflow-y-auto">
                  {comparisonHistory.map(round => {
                    const winner = round.results.find(r => r.vote === 'winner');
                    return (
                      <div
                        key={round.id}
                        className="flex items-center justify-between p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
                      >
                        <div className="flex-1">
                          <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-1">
                            {round.prompt}
                          </p>
                          <div className="flex items-center gap-2 mt-1">
                            <Badge variant="default" size="sm">{round.category || 'general'}</Badge>
                            <span className="text-xs text-surface-500">
                              {round.results.length} models compared
                            </span>
                          </div>
                        </div>
                        <div className="flex items-center gap-4">
                          <div className="flex items-center gap-1">
                            <Award className="w-4 h-4 text-success-500" />
                            <span className="text-sm font-medium text-success-600 dark:text-success-400">
                              {winner?.modelName}
                            </span>
                          </div>
                          <span className="text-xs text-surface-500">
                            {round.timestamp.toLocaleString()}
                          </span>
                        </div>
                      </div>
                    );
                  })}
                </div>
              </Card>
            </>
          ) : (
            <Card className="p-12 text-center">
              <div className="w-20 h-20 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-6">
                <BarChart3 className="w-10 h-10 text-surface-400" />
              </div>
              <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
                No Analytics Data Yet
              </h2>
              <p className="text-surface-500 max-w-md mx-auto mb-4">
                Run some model comparisons to see analytics, charts, and performance metrics.
              </p>
              <Button variant="primary" onClick={() => setActiveTab('compare')}>
                <GitCompare className="w-4 h-4" />
                Start Comparing
              </Button>
            </Card>
          )}
        </div>
      )}

      {/* Leaderboard Tab */}
      {activeTab === 'leaderboard' && (
        <div className="space-y-6">
          {leaderboard.length > 0 ? (
            <>
              {/* Top 3 Podium */}
              <div className="grid grid-cols-3 gap-4">
                {[1, 0, 2].map((podiumIndex) => {
                  const entry = leaderboard[podiumIndex];
                  if (!entry) return <div key={podiumIndex} />;

                  const heights = ['h-40', 'h-48', 'h-32'];
                  const bgColors = [
                    'from-gray-300 to-gray-400',
                    'from-yellow-400 to-amber-500',
                    'from-amber-600 to-orange-700',
                  ];

                  return (
                    <motion.div
                      key={entry.modelId}
                      initial={{ opacity: 0, y: 20 }}
                      animate={{ opacity: 1, y: 0 }}
                      transition={{ delay: podiumIndex * 0.1 }}
                      className="flex flex-col items-center"
                    >
                      <Card className="w-full p-4 text-center mb-4">
                        <div className="mb-2">{getRankBadge(entry.rank)}</div>
                        <h3 className="font-semibold text-surface-900 dark:text-white truncate">
                          {entry.modelName}
                        </h3>
                        <p className="text-2xl font-bold text-brand-600 dark:text-brand-400 my-2">
                          {entry.eloRating}
                        </p>
                        <div className="flex items-center justify-center gap-2 text-sm">
                          <span className="text-success-600">{entry.wins}W</span>
                          <span className="text-surface-400">/</span>
                          <span className="text-error-600">{entry.losses}L</span>
                        </div>
                        <div className="flex items-center justify-center gap-1 mt-2">
                          {entry.trend === 'up' && <TrendingUp className="w-4 h-4 text-success-500" />}
                          {entry.trend === 'down' && <TrendingDown className="w-4 h-4 text-error-500" />}
                          <span className={clsx(
                            'text-sm font-medium',
                            entry.lastChange > 0 ? 'text-success-600' : entry.lastChange < 0 ? 'text-error-600' : 'text-surface-500'
                          )}>
                            {entry.lastChange > 0 ? '+' : ''}{entry.lastChange}
                          </span>
                        </div>
                      </Card>
                      <div className={clsx(
                        'w-full rounded-t-lg bg-gradient-to-b flex items-end justify-center',
                        heights[podiumIndex],
                        bgColors[podiumIndex]
                      )}>
                        <span className="text-white text-4xl font-bold mb-4">{entry.rank}</span>
                      </div>
                    </motion.div>
                  );
                })}
              </div>

              {/* Full Leaderboard Table */}
              <Card className="p-6">
                <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <Trophy className="w-5 h-5 text-yellow-500" />
                  Complete Leaderboard
                </h3>
                <div className="overflow-x-auto">
                  <table className="w-full text-sm">
                    <thead>
                      <tr className="border-b border-surface-200 dark:border-surface-700">
                        <th className="text-left py-3 px-4 font-semibold text-surface-500">Rank</th>
                        <th className="text-left py-3 px-4 font-semibold text-surface-500">Model</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">ELO Rating</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Win Rate</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">W/L</th>
                        <th className="text-center py-3 px-4 font-semibold text-surface-500">Trend</th>
                      </tr>
                    </thead>
                    <tbody>
                      {leaderboard.map((entry) => (
                        <tr key={entry.modelId} className="border-b border-surface-100 dark:border-surface-800">
                          <td className="py-3 px-4">
                            <div className="flex items-center gap-2">
                              {getRankBadge(entry.rank)}
                            </div>
                          </td>
                          <td className="py-3 px-4 font-medium text-surface-900 dark:text-white">
                            {entry.modelName}
                          </td>
                          <td className="text-center py-3 px-4">
                            <span className="text-lg font-bold text-brand-600 dark:text-brand-400">
                              {entry.eloRating}
                            </span>
                          </td>
                          <td className="text-center py-3 px-4">
                            <div className="flex items-center justify-center gap-2">
                              <Progress value={entry.winRate} variant={entry.winRate > 50 ? 'success' : 'default'} className="w-20" />
                              <span className="text-surface-600 dark:text-surface-400">{entry.winRate.toFixed(0)}%</span>
                            </div>
                          </td>
                          <td className="text-center py-3 px-4">
                            <span className="text-success-600">{entry.wins}</span>
                            <span className="text-surface-400 mx-1">/</span>
                            <span className="text-error-600">{entry.losses}</span>
                          </td>
                          <td className="text-center py-3 px-4">
                            <div className="flex items-center justify-center gap-1">
                              {entry.trend === 'up' && <TrendingUp className="w-4 h-4 text-success-500" />}
                              {entry.trend === 'down' && <TrendingDown className="w-4 h-4 text-error-500" />}
                              {entry.trend === 'stable' && <span className="w-4 h-4 text-surface-400">—</span>}
                              <span className={clsx(
                                'text-sm',
                                entry.lastChange > 0 ? 'text-success-600' : entry.lastChange < 0 ? 'text-error-600' : 'text-surface-500'
                              )}>
                                {entry.lastChange > 0 ? '+' : ''}{entry.lastChange}
                              </span>
                            </div>
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </Card>

              {/* ELO Rating Explanation */}
              <Card className="p-6">
                <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <Gauge className="w-5 h-5 text-ai-purple" />
                  About ELO Ratings
                </h3>
                <div className="grid grid-cols-3 gap-4 text-sm text-surface-600 dark:text-surface-400">
                  <div className="p-4 rounded-lg bg-surface-50 dark:bg-surface-800">
                    <p className="font-semibold text-surface-900 dark:text-white mb-1">Base Rating</p>
                    <p>All models start at {BASE_ELO} ELO points</p>
                  </div>
                  <div className="p-4 rounded-lg bg-surface-50 dark:bg-surface-800">
                    <p className="font-semibold text-surface-900 dark:text-white mb-1">K-Factor: {K_FACTOR}</p>
                    <p>Maximum points change per match</p>
                  </div>
                  <div className="p-4 rounded-lg bg-surface-50 dark:bg-surface-800">
                    <p className="font-semibold text-surface-900 dark:text-white mb-1">Expected Outcome</p>
                    <p>Higher rated models gain fewer points for wins</p>
                  </div>
                </div>
              </Card>
            </>
          ) : (
            <Card className="p-12 text-center">
              <div className="w-20 h-20 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-6">
                <Trophy className="w-10 h-10 text-surface-400" />
              </div>
              <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
                No Leaderboard Data Yet
              </h2>
              <p className="text-surface-500 max-w-md mx-auto mb-4">
                Run comparisons and vote for the best responses to build the leaderboard.
              </p>
              <Button variant="primary" onClick={() => setActiveTab('compare')}>
                <GitCompare className="w-4 h-4" />
                Start Comparing
              </Button>
            </Card>
          )}
        </div>
      )}
    </div>
  );
}
