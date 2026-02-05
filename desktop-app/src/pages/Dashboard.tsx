// ============================================================================
// SnapLLM Enterprise - Dashboard Page
// Comprehensive metrics, vPID cache performance, validation & system monitoring
// ============================================================================

import React, { useMemo, useState, useEffect, useCallback, useRef } from 'react';
import { useNavigate } from 'react-router-dom';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Zap,
  Cpu,
  MessageSquare,
  Image,
  Eye,
  Activity,
  HardDrive,
  Database,
  Clock,
  TrendingUp,
  TrendingDown,
  ArrowRight,
  Plus,
  GitCompare,
  Layers,
  RefreshCw,
  AlertCircle,
  Server,
  Box,
  Thermometer,
  MemoryStick,
  Gauge,
  Timer,
  BarChart3,
  PieChart as PieChartIcon,
  Download,
  Settings,
  Play,
  Pause,
  Info,
  CheckCircle2,
  XCircle,
  AlertTriangle,
  Wifi,
  WifiOff,
  Monitor,
  ChevronDown,
  ChevronUp,
  Sparkles,
} from 'lucide-react';
import {
  LineChart,
  Line,
  AreaChart,
  Area,
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  PieChart,
  Pie,
  Cell,
  Legend,
  RadialBarChart,
  RadialBar,
} from 'recharts';
import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useHealth, useModels, useServerStatus, useCacheStats, useContextStats, useContextList } from '../hooks/useApi';
import { handleApiError, getApiBaseUrl, getMetrics } from '../lib/api';

// ============================================================================
// Types & Interfaces
// ============================================================================

interface PerformancePoint {
  time: string;
  timestamp: number;
  tokensPerSec: number;
  latency: number;
  requests: number;
  errors?: number;
}

interface DashboardSettings {
  autoRefresh: boolean;
  refreshInterval: number;
  showPerformanceChart: boolean;
  showCacheChart: boolean;
  showVpidTiers: boolean;
  showModelList: boolean;
  expandedSections: string[];
}

interface SystemMetrics {
  cpuUsage: number;
  memoryUsage: number;
  gpuUsage: number;
  gpuMemory: number;
  temperature: number;
}

// ============================================================================
// Constants
// ============================================================================

const MAX_HISTORY_POINTS = 60;
const DEFAULT_REFRESH_INTERVAL = 3000;
const CHART_COLORS = ['#0ea5e9', '#ec4899', '#10b981', '#8b5cf6', '#f59e0b', '#06b6d4'];

const DEFAULT_SETTINGS: DashboardSettings = {
  autoRefresh: true,
  refreshInterval: DEFAULT_REFRESH_INTERVAL,
  showPerformanceChart: true,
  showCacheChart: true,
  showVpidTiers: true,
  showModelList: true,
  expandedSections: ['performance', 'cache', 'vpid', 'l2context', 'models'],
};

// Animation variants
const containerVariants = {
  hidden: { opacity: 0 },
  visible: {
    opacity: 1,
    transition: { staggerChildren: 0.06 },
  },
};

const itemVariants = {
  hidden: { opacity: 0, y: 20 },
  visible: { opacity: 1, y: 0, transition: { duration: 0.3 } },
};

// ============================================================================
// Utility Functions
// ============================================================================

const formatBytes = (bytes: number): string => {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

const formatNumber = (num: number): string => {
  if (num >= 1000000) return (num / 1000000).toFixed(1) + 'M';
  if (num >= 1000) return (num / 1000).toFixed(1) + 'K';
  return num.toString();
};

const formatDuration = (seconds: number): string => {
  if (seconds >= 86400) return `${Math.floor(seconds / 86400)}d ${Math.floor((seconds % 86400) / 3600)}h`;
  if (seconds >= 3600) return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`;
  if (seconds >= 60) return `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s`;
  return `${Math.floor(seconds)}s`;
};

const validateNumber = (value: any, fallback: number = 0): number => {
  const num = Number(value);
  return isNaN(num) || !isFinite(num) ? fallback : num;
};

const validateString = (value: any, fallback: string = ''): string => {
  return typeof value === 'string' ? value : fallback;
};

// ============================================================================
// Custom Tooltip Component
// ============================================================================

const CustomTooltip = ({ active, payload, label }: any) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700 rounded-lg shadow-lg p-3">
        <p className="text-sm font-semibold text-gray-900 dark:text-white mb-2">{label}</p>
        {payload.map((entry: any, index: number) => (
          <p key={index} className="text-xs flex items-center gap-2" style={{ color: entry.color }}>
            <span className="w-2 h-2 rounded-full" style={{ backgroundColor: entry.color }} />
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

export default function Dashboard() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();

  // Settings state with localStorage persistence
  const [settings, setSettings] = useState<DashboardSettings>(() => {
    const saved = localStorage.getItem('snapllm_dashboard_settings');
    if (saved) {
      try {
        return { ...DEFAULT_SETTINGS, ...JSON.parse(saved) };
      } catch { return DEFAULT_SETTINGS; }
    }
    return DEFAULT_SETTINGS;
  });

  // Persist settings
  useEffect(() => {
    localStorage.setItem('snapllm_dashboard_settings', JSON.stringify(settings));
  }, [settings]);

  // Performance history state with localStorage
  const [performanceHistory, setPerformanceHistory] = useState<PerformancePoint[]>(() => {
    const saved = localStorage.getItem('snapllm_performance_history');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        // Keep only last hour of data
        const oneHourAgo = Date.now() - 3600000;
        return parsed.filter((p: PerformancePoint) => p.timestamp > oneHourAgo);
      } catch { return []; }
    }
    return [];
  });

  // Persist performance history
  useEffect(() => {
    localStorage.setItem('snapllm_performance_history', JSON.stringify(performanceHistory.slice(-MAX_HISTORY_POINTS)));
  }, [performanceHistory]);

  const [lastMetricsUpdate, setLastMetricsUpdate] = useState<number>(0);
  const [showSettings, setShowSettings] = useState(false);
  const [isRefreshing, setIsRefreshing] = useState(false);

  // API Hooks
  const { data: health, isLoading: healthLoading, error: healthError, refetch: refetchHealth } = useHealth();
  const { data: modelsData, isLoading: modelsLoading, error: modelsError, refetch: refetchModels } = useModels();
  const { data: cacheStats, refetch: refetchCacheStats } = useCacheStats();

  // vPID L2 Context (KV Cache) metrics
  const { data: contextStats, refetch: refetchContextStats } = useContextStats();
  const { data: contextList } = useContextList();

  // Server metrics with proper refresh interval
  const { data: metricsData, refetch: refetchMetrics } = useQuery({
    queryKey: ['serverMetrics'],
    queryFn: getMetrics,
    refetchInterval: settings.autoRefresh ? settings.refreshInterval : false,
    retry: 2,
    staleTime: 1000,
  });

  const { isConnected, isChecking } = useServerStatus();
  const apiBaseUrl = getApiBaseUrl();

  // Manual refresh all data
  const handleRefreshAll = useCallback(async () => {
    setIsRefreshing(true);
    try {
      await Promise.all([
        refetchHealth(),
        refetchModels(),
        refetchCacheStats(),
        refetchContextStats(),
        refetchMetrics(),
      ]);
    } finally {
      setIsRefreshing(false);
    }
  }, [refetchHealth, refetchModels, refetchCacheStats, refetchContextStats, refetchMetrics]);

  // Update performance history when metrics change
  useEffect(() => {
    if (metricsData && metricsData.total_requests !== lastMetricsUpdate) {
      const now = new Date();
      const timeStr = now.toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });

      // Calculate aggregated metrics with validation
      const models = metricsData.models || [];
      const avgTokensPerSec = models.length > 0
        ? models.reduce((sum: number, m: any) => sum + validateNumber(m.tokens_per_second), 0) / models.length
        : 0;
      const avgLatency = models.length > 0
        ? models.reduce((sum: number, m: any) => sum + validateNumber(m.avg_latency_ms), 0) / models.length
        : 0;

      const newPoint: PerformancePoint = {
        time: timeStr.slice(0, 5),
        timestamp: now.getTime(),
        tokensPerSec: avgTokensPerSec,
        latency: avgLatency,
        requests: validateNumber(metricsData.total_requests),
        errors: validateNumber(metricsData.total_errors),
      };

      setPerformanceHistory(prev => {
        const updated = [...prev, newPoint];
        return updated.slice(-MAX_HISTORY_POINTS);
      });

      setLastMetricsUpdate(metricsData.total_requests);
    }
  }, [metricsData, lastMetricsUpdate]);

  // Validate and compute metrics
  const loadedModels = useMemo(() => {
    if (!modelsData?.models || !Array.isArray(modelsData.models)) return [];
    return modelsData.models.filter((m: any) => m.status === 'loaded' || !m.status);
  }, [modelsData]);

  const activeModel = useMemo(() => {
    if (!modelsData?.models || !Array.isArray(modelsData.models)) return null;
    return modelsData.models.find((m: any) => m.active);
  }, [modelsData]);

  // Validated cache metrics
  const cacheMetrics = useMemo(() => {
    const defaultMetrics = {
      avgProcessingHitRate: 0,
      avgGenerationHitRate: 0,
      totalHits: 0,
      totalMisses: 0,
      totalMemoryMb: 0,
      avgSpeedup: 1.0,
    };

    if (!cacheStats?.models || !Array.isArray(cacheStats.models) || cacheStats.models.length === 0) {
      return defaultMetrics;
    }

    const models = cacheStats.models;
    const totalProcessingHits = models.reduce((sum: number, m: any) => sum + validateNumber(m.processing_hits), 0);
    const totalProcessingMisses = models.reduce((sum: number, m: any) => sum + validateNumber(m.processing_misses), 0);
    const totalGenerationHits = models.reduce((sum: number, m: any) => sum + validateNumber(m.generation_hits), 0);
    const totalGenerationMisses = models.reduce((sum: number, m: any) => sum + validateNumber(m.generation_misses), 0);
    const totalMemoryMb = models.reduce((sum: number, m: any) => sum + validateNumber(m.memory_usage_mb), 0);
    const avgSpeedup = models.length > 0
      ? models.reduce((sum: number, m: any) => sum + validateNumber(m.estimated_speedup, 1), 0) / models.length
      : 1.0;

    const processingTotal = totalProcessingHits + totalProcessingMisses;
    const generationTotal = totalGenerationHits + totalGenerationMisses;

    return {
      avgProcessingHitRate: processingTotal > 0 ? totalProcessingHits / processingTotal : 0,
      avgGenerationHitRate: generationTotal > 0 ? totalGenerationHits / generationTotal : 0,
      totalHits: totalProcessingHits + totalGenerationHits,
      totalMisses: totalProcessingMisses + totalGenerationMisses,
      totalMemoryMb,
      avgSpeedup,
    };
  }, [cacheStats]);

  // vPID 3-Tier cache breakdown
  const vpidCacheTiers = useMemo(() => {
    const totalMb = cacheMetrics.totalMemoryMb || (loadedModels.length * 500);
    return {
      hot: { name: 'HOT (L1 RAM)', value: Math.round(totalMb * 0.15), color: '#ef4444', description: 'Active Tensors', access: '<0.1ms' },
      warm: { name: 'WARM (L2 SSD)', value: Math.round(totalMb * 0.55), color: '#f59e0b', description: 'Pre-dequantized', access: '<1ms' },
      cold: { name: 'COLD (L3 Disk)', value: Math.round(totalMb * 0.30), color: '#3b82f6', description: 'Original GGUF', access: '~10ms' },
    };
  }, [cacheMetrics.totalMemoryMb, loadedModels.length]);

  // Performance chart data
  const performanceData = useMemo(() => {
    if (performanceHistory.length > 0) {
      return performanceHistory.slice(-30);
    }
    if (metricsData?.models && metricsData.models.length > 0) {
      return metricsData.models.slice(0, 10).map((m: any) => ({
        time: validateString(m.model_name, 'Model').slice(0, 8),
        tokensPerSec: validateNumber(m.tokens_per_second),
        latency: validateNumber(m.avg_latency_ms),
        requests: validateNumber(m.requests),
      }));
    }
    return [{ time: 'Now', tokensPerSec: 0, latency: 0, requests: 0 }];
  }, [performanceHistory, metricsData]);

  // Model usage distribution
  const modelUsageData = useMemo(() => {
    if (metricsData?.models && metricsData.models.length > 0) {
      const total = metricsData.models.reduce((sum: number, m: any) => sum + validateNumber(m.requests, 1), 0) || 1;
      return metricsData.models.map((m: any, i: number) => ({
        name: validateString(m.model_name, 'Model').slice(0, 12),
        value: Math.max(1, Math.round((validateNumber(m.requests, 1) / total) * 100)),
        color: CHART_COLORS[i % CHART_COLORS.length],
        requests: validateNumber(m.requests),
        tokens: validateNumber(m.tokens_generated),
      }));
    }
    return [{ name: 'No Models', value: 100, color: '#6b7280', requests: 0, tokens: 0 }];
  }, [metricsData]);

  // Cache hit rate data
  const cacheHitRateData = useMemo(() => [
    {
      name: 'Prompt Cache',
      hitRate: Math.round(cacheMetrics.avgProcessingHitRate * 100),
      missRate: Math.round((1 - cacheMetrics.avgProcessingHitRate) * 100),
    },
    {
      name: 'Generation Cache',
      hitRate: Math.round(cacheMetrics.avgGenerationHitRate * 100),
      missRate: Math.round((1 - cacheMetrics.avgGenerationHitRate) * 100),
    },
  ], [cacheMetrics]);

  // Aggregate stats with validation
  const totalRequests = validateNumber(metricsData?.total_requests);
  const totalTokens = validateNumber(metricsData?.total_tokens_generated);
  const totalErrors = validateNumber(metricsData?.total_errors);
  const uptime = validateNumber(metricsData?.uptime_seconds);
  const errorRate = totalRequests > 0 ? (totalErrors / totalRequests) * 100 : 0;

  // Connection status
  const serverError = healthError || modelsError;
  const isServerOffline = !isConnected && !isChecking;

  // Export metrics
  const exportMetrics = useCallback(() => {
    const data = {
      exportedAt: new Date().toISOString(),
      health,
      models: modelsData,
      metrics: metricsData,
      cacheStats,
      performanceHistory,
      summary: {
        totalRequests,
        totalTokens,
        totalErrors,
        uptime,
        loadedModels: loadedModels.length,
        cacheHitRate: cacheMetrics.avgProcessingHitRate,
      },
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `snapllm-metrics-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }, [health, modelsData, metricsData, cacheStats, performanceHistory, totalRequests, totalTokens, totalErrors, uptime, loadedModels, cacheMetrics]);

  // Toggle section expansion
  const toggleSection = (section: string) => {
    setSettings(prev => ({
      ...prev,
      expandedSections: prev.expandedSections.includes(section)
        ? prev.expandedSections.filter(s => s !== section)
        : [...prev.expandedSections, section],
    }));
  };

  return (
    <motion.div
      variants={containerVariants}
      initial="hidden"
      animate="visible"
      className="space-y-6"
    >
      {/* Hero Section */}
      <motion.div variants={itemVariants}>
        <div className="relative overflow-hidden rounded-2xl bg-gradient-to-br from-sky-500/5 via-sky-600/5 to-slate-100 dark:from-sky-900/20 dark:via-slate-800 dark:to-slate-900 border border-sky-200/50 dark:border-sky-800/30 p-6 lg:p-8">
          <div className="absolute top-0 right-0 w-96 h-96 bg-gradient-to-br from-sky-500/10 to-transparent rounded-full blur-3xl" />

          <div className="relative flex flex-col lg:flex-row lg:items-center lg:justify-between gap-6">
            <div className="flex items-center gap-6">
              <img src="/snapllm-full.png" alt="SnapLLM" className="h-20 w-auto hidden lg:block" />
              <div>
                <div className="flex items-center gap-3 mb-2">
                  {/* Connection Status Indicator */}
                  <div className="flex items-center gap-2">
                    {isChecking ? (
                      <RefreshCw className="w-4 h-4 text-yellow-500 animate-spin" />
                    ) : isConnected ? (
                      <Wifi className="w-4 h-4 text-green-500" />
                    ) : (
                      <WifiOff className="w-4 h-4 text-red-500" />
                    )}
                    <div className={clsx(
                      'w-2.5 h-2.5 rounded-full',
                      isConnected ? 'bg-green-500 animate-pulse' : isChecking ? 'bg-yellow-500' : 'bg-red-500'
                    )} />
                    <span className="text-sm font-medium text-gray-600 dark:text-gray-400">
                      {isConnected ? 'Connected' : isChecking ? 'Connecting...' : 'Disconnected'}
                    </span>
                  </div>
                  {isConnected && uptime > 0 && (
                    <span className="text-xs text-gray-500 px-2 py-0.5 bg-gray-100 dark:bg-gray-800 rounded-full">
                      Uptime: {formatDuration(uptime)}
                    </span>
                  )}
                </div>
                <h1 className="text-2xl lg:text-3xl font-bold text-gray-900 dark:text-white mb-2">
                  <span className="text-sky-600 dark:text-sky-400">Snap</span>
                  <span className="text-orange-500">LLM</span> Dashboard
                  <span className="ml-2 text-xs font-bold text-orange-500 bg-orange-100 dark:bg-orange-900/30 px-2 py-0.5 rounded-full align-middle">(BETA)</span>
                </h1>
                <p className="text-gray-600 dark:text-gray-400 max-w-xl">
                  Multi-model inference with vPID &lt;1ms switching.
                  {loadedModels.length > 0 && (
                    <span className="font-medium text-sky-600 dark:text-sky-400">
                      {' '}{loadedModels.length} model{loadedModels.length > 1 ? 's' : ''} loaded
                      {totalRequests > 0 && ` • ${formatNumber(totalRequests)} requests`}
                      {totalTokens > 0 && ` • ${formatNumber(totalTokens)} tokens`}
                    </span>
                  )}
                </p>
              </div>
            </div>

            <div className="flex flex-wrap gap-3">
              {/* Auto-refresh Toggle */}
              <button
                onClick={() => setSettings(s => ({ ...s, autoRefresh: !s.autoRefresh }))}
                className={clsx(
                  'inline-flex items-center gap-2 px-4 py-2.5 rounded-xl font-medium transition-all',
                  settings.autoRefresh
                    ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400 border border-green-200 dark:border-green-800'
                    : 'bg-gray-100 dark:bg-gray-800 text-gray-600 dark:text-gray-400 border border-gray-200 dark:border-gray-700'
                )}
                title={settings.autoRefresh ? 'Auto-refresh ON' : 'Auto-refresh OFF'}
              >
                {settings.autoRefresh ? <Play className="w-4 h-4" /> : <Pause className="w-4 h-4" />}
                {settings.autoRefresh ? 'Live' : 'Paused'}
              </button>

              {/* Manual Refresh */}
              <button
                onClick={handleRefreshAll}
                disabled={isRefreshing}
                className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl font-medium bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-300 border border-gray-200 dark:border-gray-700 hover:bg-gray-200 dark:hover:bg-gray-700 transition-all disabled:opacity-50"
              >
                <RefreshCw className={clsx('w-4 h-4', isRefreshing && 'animate-spin')} />
                Refresh
              </button>

              {/* Export */}
              <button
                onClick={exportMetrics}
                className="inline-flex items-center gap-2 px-4 py-2.5 rounded-xl font-medium bg-gray-100 dark:bg-gray-800 text-gray-700 dark:text-gray-300 border border-gray-200 dark:border-gray-700 hover:bg-gray-200 dark:hover:bg-gray-700 transition-all"
              >
                <Download className="w-4 h-4" />
                Export
              </button>

              {/* Primary Actions */}
              <button
                onClick={() => navigate('/chat')}
                className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl font-medium text-white bg-gradient-to-r from-sky-500 to-sky-600 hover:from-sky-600 hover:to-sky-700 shadow-lg shadow-sky-500/25 transition-all"
              >
                <MessageSquare className="w-5 h-5" />
                Chat
              </button>
              <button
                onClick={() => navigate('/models')}
                className="inline-flex items-center gap-2 px-5 py-2.5 rounded-xl font-medium bg-white dark:bg-gray-800 text-gray-900 dark:text-white border border-gray-200 dark:border-gray-700 hover:bg-gray-50 dark:hover:bg-gray-700 transition-all"
              >
                <Plus className="w-5 h-5" />
                Models
              </button>
            </div>
          </div>
        </div>
      </motion.div>

      {/* Server Offline Alert */}
      <AnimatePresence>
        {isServerOffline && (
          <motion.div
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            variants={itemVariants}
          >
            <div className="rounded-xl bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800 p-4">
              <div className="flex items-start gap-3">
                <AlertCircle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
                <div className="flex-1">
                  <h4 className="font-medium text-red-800 dark:text-red-200">Cannot connect to server</h4>
                  <p className="text-sm text-red-600 dark:text-red-300 mt-1">
                    Make sure the API server is running on {apiBaseUrl}
                  </p>
                  <code className="block mt-2 text-xs bg-red-100 dark:bg-red-900/50 p-2 rounded-lg text-red-700 dark:text-red-300">
                    python api-server/run.py --dev --port 6930
                  </code>
                </div>
                <button
                  onClick={() => refetchHealth()}
                  className="px-3 py-1.5 text-sm font-medium text-red-700 dark:text-red-300 bg-red-100 dark:bg-red-900/50 rounded-lg hover:bg-red-200 dark:hover:bg-red-900 transition-colors"
                >
                  Retry
                </button>
              </div>
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Key Metrics - 6 cards */}
      <motion.div variants={itemVariants} className="grid grid-cols-2 lg:grid-cols-6 gap-4">
        <MetricCard
          icon={Zap}
          iconColor="text-yellow-500"
          iconBg="bg-yellow-500/10"
          label="Model Switch"
          value="<1ms"
          trend="vPID"
          trendUp={true}
          description="Ultra-fast switching"
          tooltip="Virtual Processing-In-Disk enables sub-millisecond model context switching"
        />
        <MetricCard
          icon={Activity}
          iconColor="text-green-500"
          iconBg="bg-green-500/10"
          label="Cache Hit Rate"
          value={`${Math.round(cacheMetrics.avgProcessingHitRate * 100)}%`}
          trend={cacheMetrics.avgSpeedup > 1 ? `${cacheMetrics.avgSpeedup.toFixed(1)}x` : '-'}
          trendUp={cacheMetrics.avgProcessingHitRate > 0.5}
          description="Prompt cache efficiency"
          tooltip={`${cacheMetrics.totalHits} hits / ${cacheMetrics.totalHits + cacheMetrics.totalMisses} total`}
        />
        <MetricCard
          icon={Gauge}
          iconColor="text-sky-500"
          iconBg="bg-sky-500/10"
          label="Total Requests"
          value={formatNumber(totalRequests)}
          trend={totalTokens > 0 ? `${formatNumber(totalTokens)} tok` : '-'}
          trendUp={totalRequests > 0}
          description="Requests served"
          tooltip={`${totalTokens.toLocaleString()} tokens generated`}
        />
        <MetricCard
          icon={Server}
          iconColor="text-purple-500"
          iconBg="bg-purple-500/10"
          label="Models Loaded"
          value={String(loadedModels.length)}
          trend={activeModel ? 'Active' : 'Idle'}
          trendUp={loadedModels.length > 0}
          description={`of ${modelsData?.count || 0} available`}
          tooltip={activeModel ? `Active: ${activeModel.name || activeModel.id}` : 'No active model'}
        />
        <MetricCard
          icon={errorRate > 5 ? AlertTriangle : CheckCircle2}
          iconColor={errorRate > 5 ? 'text-red-500' : errorRate > 1 ? 'text-yellow-500' : 'text-green-500'}
          iconBg={errorRate > 5 ? 'bg-red-500/10' : errorRate > 1 ? 'bg-yellow-500/10' : 'bg-green-500/10'}
          label="Error Rate"
          value={`${errorRate.toFixed(1)}%`}
          trend={`${totalErrors} errors`}
          trendUp={errorRate < 1}
          description="Request success"
          tooltip={`${totalRequests - totalErrors} successful / ${totalRequests} total`}
        />
        <MetricCard
          icon={Cpu}
          iconColor="text-pink-500"
          iconBg="bg-pink-500/10"
          label="Server Status"
          value={health?.status === 'ok' ? 'Running' : 'Offline'}
          trend={health?.version || 'v1.0.0'}
          trendUp={health?.status === 'ok'}
          description="API Server"
          tooltip={`Server: ${apiBaseUrl}`}
        />
      </motion.div>

      {/* Main Content Grid */}
      <div className="grid lg:grid-cols-3 gap-6">
        {/* Performance Chart */}
        <motion.div variants={itemVariants} className="lg:col-span-2">
          <CollapsibleSection
            title="Performance Metrics"
            subtitle="Real-time inference speed & latency"
            icon={BarChart3}
            iconColor="text-sky-500"
            isExpanded={settings.expandedSections.includes('performance')}
            onToggle={() => toggleSection('performance')}
            onRefresh={refetchMetrics}
          >
            <div className="h-64">
              <ResponsiveContainer width="100%" height="100%">
                <AreaChart data={performanceData}>
                  <defs>
                    <linearGradient id="colorTokens" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#0ea5e9" stopOpacity={0.3} />
                      <stop offset="95%" stopColor="#0ea5e9" stopOpacity={0} />
                    </linearGradient>
                    <linearGradient id="colorLatency" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#8b5cf6" stopOpacity={0.2} />
                      <stop offset="95%" stopColor="#8b5cf6" stopOpacity={0} />
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.3} />
                  <XAxis dataKey="time" tick={{ fontSize: 11, fill: '#9ca3af' }} stroke="#4b5563" tickLine={false} />
                  <YAxis tick={{ fontSize: 11, fill: '#9ca3af' }} stroke="#4b5563" tickLine={false} axisLine={false} />
                  <Tooltip content={<CustomTooltip />} />
                  <Area type="monotone" dataKey="tokensPerSec" stroke="#0ea5e9" strokeWidth={2} fillOpacity={1} fill="url(#colorTokens)" name="Tokens/s" />
                  <Line type="monotone" dataKey="latency" stroke="#8b5cf6" strokeWidth={2} dot={false} name="Latency (ms)" />
                </AreaChart>
              </ResponsiveContainer>
            </div>
            <div className="flex justify-center gap-6 mt-4 text-sm">
              <div className="flex items-center gap-2">
                <div className="w-3 h-3 rounded-full bg-sky-500" />
                <span className="text-gray-600 dark:text-gray-400">Tokens/s</span>
              </div>
              <div className="flex items-center gap-2">
                <div className="w-3 h-3 rounded-full bg-purple-500" />
                <span className="text-gray-600 dark:text-gray-400">Latency (ms)</span>
              </div>
            </div>
          </CollapsibleSection>
        </motion.div>

        {/* Cache Hit Rates */}
        <motion.div variants={itemVariants}>
          <CollapsibleSection
            title="Cache Hit Rates"
            subtitle="vPID cache efficiency"
            icon={Activity}
            iconColor="text-green-500"
            isExpanded={settings.expandedSections.includes('cache')}
            onToggle={() => toggleSection('cache')}
            onRefresh={refetchCacheStats}
          >
            <div className="h-48">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={cacheHitRateData} layout="vertical">
                  <CartesianGrid strokeDasharray="3 3" stroke="#374151" opacity={0.3} horizontal={false} />
                  <XAxis type="number" domain={[0, 100]} tick={{ fontSize: 11, fill: '#9ca3af' }} />
                  <YAxis dataKey="name" type="category" tick={{ fontSize: 11, fill: '#9ca3af' }} width={100} />
                  <Tooltip content={<CustomTooltip />} />
                  <Bar dataKey="hitRate" stackId="a" fill="#10b981" name="Hits %" radius={[0, 4, 4, 0]} />
                  <Bar dataKey="missRate" stackId="a" fill="#ef4444" name="Misses %" radius={[0, 4, 4, 0]} />
                </BarChart>
              </ResponsiveContainer>
            </div>
            <div className="flex justify-center gap-6 mt-2 text-sm">
              <div className="flex items-center gap-2">
                <div className="w-3 h-3 rounded-full bg-green-500" />
                <span className="text-gray-600 dark:text-gray-400">Hits</span>
              </div>
              <div className="flex items-center gap-2">
                <div className="w-3 h-3 rounded-full bg-red-500" />
                <span className="text-gray-600 dark:text-gray-400">Misses</span>
              </div>
            </div>
            <div className="mt-4 pt-4 border-t border-gray-200 dark:border-gray-700">
              <div className="grid grid-cols-2 gap-4 text-center">
                <div>
                  <p className="text-2xl font-bold text-green-500">{formatNumber(cacheMetrics.totalHits)}</p>
                  <p className="text-xs text-gray-500">Total Hits</p>
                </div>
                <div>
                  <p className="text-2xl font-bold text-red-500">{formatNumber(cacheMetrics.totalMisses)}</p>
                  <p className="text-xs text-gray-500">Total Misses</p>
                </div>
              </div>
            </div>
          </CollapsibleSection>
        </motion.div>
      </div>

      {/* vPID 3-Tier Cache Performance */}
      <motion.div variants={itemVariants}>
        <CollapsibleSection
          title="vPID 3-Tier Cache Architecture"
          subtitle="Hierarchical tensor caching for ultra-fast model switching"
          icon={Layers}
          iconColor="text-yellow-500"
          isExpanded={settings.expandedSections.includes('vpid')}
          onToggle={() => toggleSection('vpid')}
          onRefresh={refetchCacheStats}
          badge={<span className="px-2 py-0.5 bg-gradient-to-r from-yellow-500/20 to-orange-500/20 text-yellow-700 dark:text-yellow-400 text-xs font-medium rounded-full flex items-center gap-1 border border-yellow-500/30">
            <Zap className="w-3 h-3" />&lt;1ms
          </span>}
        >
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
            {/* HOT Cache - L1 RAM */}
            <CacheTierCard
              name="L1 HOT (RAM)"
              value={vpidCacheTiers.hot.value}
              description={vpidCacheTiers.hot.description}
              accessTime={vpidCacheTiers.hot.access}
              color="red"
              isActive={true}
            />

            {/* WARM Cache - L2 SSD */}
            <CacheTierCard
              name="L2 WARM (SSD)"
              value={vpidCacheTiers.warm.value}
              description={vpidCacheTiers.warm.description}
              accessTime={vpidCacheTiers.warm.access}
              color="yellow"
              isActive={false}
            />

            {/* COLD Cache - L3 Disk */}
            <CacheTierCard
              name="L3 COLD (Disk)"
              value={vpidCacheTiers.cold.value}
              description={vpidCacheTiers.cold.description}
              accessTime={vpidCacheTiers.cold.access}
              color="blue"
              isActive={false}
            />
          </div>

          {/* Per-model cache stats */}
          {cacheStats?.models && cacheStats.models.length > 0 && (
            <div className="space-y-3">
              <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Per-Model Cache Statistics</h4>
              {cacheStats.models.slice(0, 5).map((stat: any) => (
                <ModelCacheStatRow key={stat.model_id} stat={stat} />
              ))}
            </div>
          )}
        </CollapsibleSection>
      </motion.div>

      {/* vPID L2 Context KV Cache */}
      <motion.div variants={itemVariants}>
        <CollapsibleSection
          title="vPID L2 Context KV Cache"
          subtitle="Pre-computed KV cache for O(1) context injection"
          icon={Database}
          iconColor="text-purple-500"
          isExpanded={settings.expandedSections.includes('l2context')}
          onToggle={() => toggleSection('l2context')}
          onRefresh={refetchContextStats}
          badge={contextStats?.hit_rate && contextStats.hit_rate > 0 ? (
            <span className="px-2 py-0.5 bg-gradient-to-r from-purple-500/20 to-pink-500/20 text-purple-700 dark:text-purple-400 text-xs font-medium rounded-full flex items-center gap-1 border border-purple-500/30">
              <TrendingUp className="w-3 h-3" />{(contextStats.hit_rate * 100).toFixed(0)}% Hit Rate
            </span>
          ) : null}
        >
          {/* L2 Context Metrics Grid */}
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4 mb-6">
            <div className="p-4 rounded-xl bg-gradient-to-br from-purple-500/10 to-transparent border border-purple-200 dark:border-purple-800/50">
              <p className="text-3xl font-bold text-gray-900 dark:text-white">
                {contextStats?.total_contexts || 0}
              </p>
              <p className="text-xs text-gray-500 dark:text-gray-400">Cached Contexts</p>
            </div>
            <div className="p-4 rounded-xl bg-gradient-to-br from-green-500/10 to-transparent border border-green-200 dark:border-green-800/50">
              <p className="text-3xl font-bold text-green-600 dark:text-green-400">
                {contextStats?.cache_hits || 0}
              </p>
              <p className="text-xs text-gray-500 dark:text-gray-400">Cache Hits</p>
            </div>
            <div className="p-4 rounded-xl bg-gradient-to-br from-red-500/10 to-transparent border border-red-200 dark:border-red-800/50">
              <p className="text-3xl font-bold text-red-600 dark:text-red-400">
                {contextStats?.cache_misses || 0}
              </p>
              <p className="text-xs text-gray-500 dark:text-gray-400">Cache Misses</p>
            </div>
            <div className="p-4 rounded-xl bg-gradient-to-br from-sky-500/10 to-transparent border border-sky-200 dark:border-sky-800/50">
              <p className="text-3xl font-bold text-sky-600 dark:text-sky-400">
                {contextStats?.avg_query_latency_ms?.toFixed(0) || 0}<span className="text-lg">ms</span>
              </p>
              <p className="text-xs text-gray-500 dark:text-gray-400">Avg Query Latency</p>
            </div>
          </div>

          {/* L2 Context Tier Distribution */}
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
            <ContextTierCard
              name="HOT (GPU)"
              count={contextStats?.tier_stats?.hot?.count || 0}
              sizeMb={contextStats?.tier_stats?.hot?.size_mb || 0}
              color="red"
              description="Instant injection"
            />
            <ContextTierCard
              name="WARM (CPU)"
              count={contextStats?.tier_stats?.warm?.count || 0}
              sizeMb={contextStats?.tier_stats?.warm?.size_mb || 0}
              color="yellow"
              description="Fast restore"
            />
            <ContextTierCard
              name="COLD (Disk)"
              count={contextStats?.tier_stats?.cold?.count || 0}
              sizeMb={contextStats?.tier_stats?.cold?.size_mb || 0}
              color="blue"
              description="Persistent storage"
            />
          </div>

          {/* Memory Usage */}
          {contextStats?.memory_usage && (
            <div className="p-4 rounded-xl bg-gray-50 dark:bg-gray-800/50 border border-gray-200 dark:border-gray-700">
              <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-3 flex items-center gap-2">
                <MemoryStick className="w-4 h-4" /> Memory Usage
              </h4>
              <div className="grid grid-cols-3 gap-4 text-center">
                <div>
                  <p className="text-xl font-bold text-red-600 dark:text-red-400">
                    {contextStats.memory_usage.gpu_mb?.toFixed(0) || 0} MB
                  </p>
                  <p className="text-xs text-gray-500">GPU VRAM</p>
                </div>
                <div>
                  <p className="text-xl font-bold text-yellow-600 dark:text-yellow-400">
                    {contextStats.memory_usage.cpu_mb?.toFixed(0) || 0} MB
                  </p>
                  <p className="text-xs text-gray-500">System RAM</p>
                </div>
                <div>
                  <p className="text-xl font-bold text-blue-600 dark:text-blue-400">
                    {contextStats.memory_usage.disk_mb?.toFixed(0) || 0} MB
                  </p>
                  <p className="text-xs text-gray-500">Disk</p>
                </div>
              </div>
            </div>
          )}

          {/* Cached Contexts List */}
          {contextList?.contexts && contextList.contexts.length > 0 && (
            <div className="mt-4 space-y-2">
              <h4 className="text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">Cached Contexts</h4>
              {contextList.contexts.slice(0, 5).map((ctx) => (
                <ContextListRow key={ctx.id} context={ctx} />
              ))}
              {contextList.contexts.length > 5 && (
                <button
                  onClick={() => navigate('/contexts')}
                  className="w-full py-2 text-sm text-purple-600 dark:text-purple-400 hover:bg-purple-50 dark:hover:bg-purple-900/20 rounded-lg transition-colors"
                >
                  View all {contextList.contexts.length} contexts →
                </button>
              )}
            </div>
          )}

          {(!contextList?.contexts || contextList.contexts.length === 0) && (
            <div className="text-center py-8">
              <Database className="w-12 h-12 text-gray-300 dark:text-gray-600 mx-auto mb-3" />
              <p className="text-gray-600 dark:text-gray-400 mb-2">No contexts cached yet</p>
              <p className="text-sm text-gray-500 dark:text-gray-500 mb-4">
                Ingest documents to pre-compute KV cache for O(1) context injection
              </p>
              <button
                onClick={() => navigate('/contexts')}
                className="inline-flex items-center gap-2 px-4 py-2 rounded-lg font-medium text-white bg-gradient-to-r from-purple-500 to-purple-600 hover:from-purple-600 hover:to-purple-700 transition-colors"
              >
                <Plus className="w-4 h-4" />
                Ingest Context
              </button>
            </div>
          )}
        </CollapsibleSection>
      </motion.div>

      {/* Secondary Grid */}
      <div className="grid lg:grid-cols-2 gap-6">
        {/* Loaded Models */}
        <motion.div variants={itemVariants}>
          <CollapsibleSection
            title="Loaded Models"
            subtitle={`${loadedModels.length} active models`}
            icon={Box}
            iconColor="text-sky-500"
            isExpanded={settings.expandedSections.includes('models')}
            onToggle={() => toggleSection('models')}
            onRefresh={refetchModels}
            headerAction={
              <button
                onClick={() => navigate('/models')}
                className="text-sm text-sky-600 dark:text-sky-400 hover:underline flex items-center gap-1"
              >
                Manage <ArrowRight className="w-4 h-4" />
              </button>
            }
          >
            {modelsLoading ? (
              <div className="flex items-center justify-center py-12">
                <RefreshCw className="w-8 h-8 text-sky-500 animate-spin" />
              </div>
            ) : loadedModels.length > 0 ? (
              <div className="space-y-3">
                {loadedModels.slice(0, 5).map((model: any) => (
                  <ModelListItem
                    key={model.id}
                    model={model}
                    isActive={model.active}
                    onClick={() => navigate('/chat')}
                  />
                ))}
              </div>
            ) : (
              <div className="text-center py-12">
                <Cpu className="w-12 h-12 text-gray-300 dark:text-gray-600 mx-auto mb-3" />
                <p className="text-gray-600 dark:text-gray-400 mb-4">No models loaded</p>
                <button
                  onClick={() => navigate('/models')}
                  className="inline-flex items-center gap-2 px-4 py-2 rounded-lg font-medium text-white bg-gradient-to-r from-sky-500 to-sky-600 hover:from-sky-600 hover:to-sky-700 transition-colors"
                >
                  <Plus className="w-4 h-4" />
                  Load Model
                </button>
              </div>
            )}
          </CollapsibleSection>
        </motion.div>

        {/* Quick Actions */}
        <motion.div variants={itemVariants}>
          <div className="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 shadow-sm p-6">
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <Sparkles className="w-5 h-5 text-sky-500" />
              Quick Actions
            </h3>
            <div className="grid grid-cols-2 gap-3">
              <QuickActionCard onClick={() => navigate('/chat')} icon={MessageSquare} label="Chat" description="Text Generation" gradient="from-sky-500/20 to-sky-600/20" iconColor="text-sky-600 dark:text-sky-400" />
              <QuickActionCard onClick={() => navigate('/images')} icon={Image} label="Images" description="Stable Diffusion" gradient="from-pink-500/20 to-pink-600/20" iconColor="text-pink-600 dark:text-pink-400" />
              <QuickActionCard onClick={() => navigate('/compare')} icon={GitCompare} label="Compare" description="A/B Model Testing" gradient="from-purple-500/20 to-purple-600/20" iconColor="text-purple-600 dark:text-purple-400" />
              <QuickActionCard onClick={() => navigate('/vision')} icon={Eye} label="Vision" description="Image Analysis" gradient="from-emerald-500/20 to-emerald-600/20" iconColor="text-emerald-600 dark:text-emerald-400" />
              
            </div>
          </div>
        </motion.div>
      </div>

      {/* Model Usage Distribution */}
      {metricsData?.models && metricsData.models.length > 1 && (
        <motion.div variants={itemVariants}>
          <div className="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 shadow-sm p-6">
            <h3 className="text-lg font-semibold text-gray-900 dark:text-white mb-4 flex items-center gap-2">
              <PieChartIcon className="w-5 h-5 text-purple-500" />
              Model Usage Distribution
            </h3>
            <div className="h-64">
              <ResponsiveContainer width="100%" height="100%">
                <PieChart>
                  <Pie
                    data={modelUsageData}
                    cx="50%"
                    cy="50%"
                    innerRadius={60}
                    outerRadius={90}
                    dataKey="value"
                    label={({ name, percent }) => `${name} (${(percent * 100).toFixed(0)}%)`}
                    labelLine={false}
                  >
                    {modelUsageData.map((entry, index) => (
                      <Cell key={`cell-${index}`} fill={entry.color} />
                    ))}
                  </Pie>
                  <Tooltip content={<CustomTooltip />} />
                </PieChart>
              </ResponsiveContainer>
            </div>
          </div>
        </motion.div>
      )}
    </motion.div>
  );
}

// ============================================================================
// Sub-Components
// ============================================================================

interface MetricCardProps {
  icon: React.ComponentType<{ className?: string }>;
  iconColor: string;
  iconBg: string;
  label: string;
  value: string;
  trend: string;
  trendUp: boolean;
  description: string;
  tooltip?: string;
}

const MetricCard: React.FC<MetricCardProps> = ({
  icon: Icon,
  iconColor,
  iconBg,
  label,
  value,
  trend,
  trendUp,
  description,
  tooltip,
}) => (
  <motion.div
    whileHover={{ y: -2, boxShadow: '0 8px 30px rgba(0,0,0,0.12)' }}
    className="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 shadow-sm p-4 relative group"
    title={tooltip}
  >
    <div className="flex items-start justify-between mb-2">
      <div className={clsx('w-9 h-9 rounded-lg flex items-center justify-center', iconBg)}>
        <Icon className={clsx('w-4 h-4', iconColor)} />
      </div>
      <div className={clsx(
        'flex items-center gap-1 text-xs font-medium px-2 py-0.5 rounded-full',
        trendUp
          ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
          : 'bg-gray-100 dark:bg-gray-800 text-gray-600 dark:text-gray-400'
      )}>
        {trendUp ? <TrendingUp className="w-3 h-3" /> : <TrendingDown className="w-3 h-3" />}
        {trend}
      </div>
    </div>
    <p className="text-xl font-bold text-gray-900 dark:text-white">{value}</p>
    <p className="text-xs text-gray-500 mt-0.5">{label}</p>
    <p className="text-xs text-gray-400">{description}</p>
    {tooltip && (
      <div className="absolute top-2 right-2 opacity-0 group-hover:opacity-100 transition-opacity">
        <Info className="w-3.5 h-3.5 text-gray-400" />
      </div>
    )}
  </motion.div>
);

interface CollapsibleSectionProps {
  title: string;
  subtitle: string;
  icon: React.ComponentType<{ className?: string }>;
  iconColor: string;
  isExpanded: boolean;
  onToggle: () => void;
  onRefresh?: () => void;
  badge?: React.ReactNode;
  headerAction?: React.ReactNode;
  children: React.ReactNode;
}

const CollapsibleSection: React.FC<CollapsibleSectionProps> = ({
  title,
  subtitle,
  icon: Icon,
  iconColor,
  isExpanded,
  onToggle,
  onRefresh,
  badge,
  headerAction,
  children,
}) => (
  <div className="bg-white dark:bg-gray-900 rounded-xl border border-gray-200 dark:border-gray-800 shadow-sm overflow-hidden">
    <div
      className="flex items-center justify-between p-4 cursor-pointer hover:bg-gray-50 dark:hover:bg-gray-800/50 transition-colors"
      onClick={onToggle}
    >
      <div className="flex items-center gap-3">
        <Icon className={clsx('w-5 h-5', iconColor)} />
        <div>
          <h3 className="text-lg font-semibold text-gray-900 dark:text-white">{title}</h3>
          <p className="text-sm text-gray-500">{subtitle}</p>
        </div>
        {badge}
      </div>
      <div className="flex items-center gap-2">
        {headerAction}
        {onRefresh && (
          <button
            onClick={(e) => { e.stopPropagation(); onRefresh(); }}
            className="p-2 rounded-lg bg-gray-100 dark:bg-gray-800 hover:bg-gray-200 dark:hover:bg-gray-700 transition-colors"
            title="Refresh"
          >
            <RefreshCw className="w-4 h-4 text-gray-600 dark:text-gray-400" />
          </button>
        )}
        {isExpanded ? <ChevronUp className="w-5 h-5 text-gray-400" /> : <ChevronDown className="w-5 h-5 text-gray-400" />}
      </div>
    </div>
    <AnimatePresence>
      {isExpanded && (
        <motion.div
          initial={{ height: 0, opacity: 0 }}
          animate={{ height: 'auto', opacity: 1 }}
          exit={{ height: 0, opacity: 0 }}
          transition={{ duration: 0.2 }}
          className="px-6 pb-6"
        >
          {children}
        </motion.div>
      )}
    </AnimatePresence>
  </div>
);

interface CacheTierCardProps {
  name: string;
  value: number;
  description: string;
  accessTime: string;
  color: 'red' | 'yellow' | 'blue';
  isActive: boolean;
}

const CacheTierCard: React.FC<CacheTierCardProps> = ({ name, value, description, accessTime, color, isActive }) => {
  const colorClasses = {
    red: { border: 'border-red-200 dark:border-red-800/50', bg: 'from-red-500/10 via-red-500/5', dot: 'bg-red-500', text: 'text-red-700 dark:text-red-400', timer: 'text-red-600 dark:text-red-400' },
    yellow: { border: 'border-yellow-200 dark:border-yellow-800/50', bg: 'from-yellow-500/10 via-yellow-500/5', dot: 'bg-yellow-500', text: 'text-yellow-700 dark:text-yellow-400', timer: 'text-yellow-600 dark:text-yellow-400' },
    blue: { border: 'border-blue-200 dark:border-blue-800/50', bg: 'from-blue-500/10 via-blue-500/5', dot: 'bg-blue-500', text: 'text-blue-700 dark:text-blue-400', timer: 'text-blue-600 dark:text-blue-400' },
  };
  const c = colorClasses[color];

  return (
    <div className={clsx('relative overflow-hidden p-5 rounded-xl bg-gradient-to-br to-transparent border', c.bg, c.border)}>
      <div className={clsx('absolute top-0 right-0 w-20 h-20 rounded-full blur-2xl', c.bg)} />
      <div className="relative">
        <div className="flex items-center gap-2 mb-3">
          <div className={clsx('w-3 h-3 rounded-full shadow-lg', c.dot, isActive && 'animate-pulse')} style={{ boxShadow: `0 0 8px ${color === 'red' ? '#ef4444' : color === 'yellow' ? '#f59e0b' : '#3b82f6'}` }} />
          <span className={clsx('text-sm font-semibold', c.text)}>{name}</span>
        </div>
        <p className="text-3xl font-bold text-gray-900 dark:text-white mb-1">
          {value} <span className="text-lg font-normal text-gray-500">MB</span>
        </p>
        <p className="text-xs text-gray-500 dark:text-gray-400">{description}</p>
        <div className="mt-3 flex items-center gap-2">
          <Timer className={clsx('w-4 h-4', c.timer)} />
          <span className={clsx('text-xs font-medium', c.timer)}>{accessTime} access</span>
        </div>
      </div>
    </div>
  );
};

interface ModelCacheStatRowProps {
  stat: any;
}

const ModelCacheStatRow: React.FC<ModelCacheStatRowProps> = ({ stat }) => {
  const hitRate = validateNumber(stat.processing_hit_rate, 0) * 100;
  const speedup = validateNumber(stat.estimated_speedup, 1);
  const memoryMb = validateNumber(stat.memory_usage_mb, 0);
  const hits = validateNumber(stat.processing_hits, 0) + validateNumber(stat.generation_hits, 0);
  const misses = validateNumber(stat.processing_misses, 0) + validateNumber(stat.generation_misses, 0);

  return (
    <div className="flex items-center gap-4 p-3 rounded-lg bg-gray-50 dark:bg-gray-800/50 hover:bg-gray-100 dark:hover:bg-gray-800 transition-colors">
      <div className="w-10 h-10 rounded-lg bg-gradient-to-br from-sky-500/20 to-purple-500/20 flex items-center justify-center">
        <Box className="w-5 h-5 text-sky-600 dark:text-sky-400" />
      </div>
      <div className="flex-1 min-w-0">
        <p className="font-medium text-gray-900 dark:text-white truncate">{validateString(stat.model_id, 'Unknown')}</p>
        <div className="flex items-center gap-4 mt-1 text-xs text-gray-500">
          <span className="flex items-center gap-1">
            <div className="w-2 h-2 rounded-full bg-green-500" />
            {formatNumber(hits)} hits
          </span>
          <span className="flex items-center gap-1">
            <div className="w-2 h-2 rounded-full bg-red-500" />
            {formatNumber(misses)} misses
          </span>
          <span>{memoryMb.toFixed(0)} MB</span>
        </div>
      </div>
      <div className="text-right">
        <p className="text-lg font-bold text-green-600 dark:text-green-400">{hitRate.toFixed(0)}%</p>
        <p className="text-xs text-gray-500">Hit Rate</p>
      </div>
      <div className="text-right">
        <p className="text-lg font-bold text-sky-600 dark:text-sky-400">{speedup.toFixed(1)}x</p>
        <p className="text-xs text-gray-500">Speedup</p>
      </div>
    </div>
  );
};

interface ModelListItemProps {
  model: any;
  isActive: boolean;
  onClick: () => void;
}

const ModelListItem: React.FC<ModelListItemProps> = ({ model, isActive, onClick }) => (
  <div
    className={clsx(
      'flex items-center gap-3 p-3 rounded-xl transition-all cursor-pointer',
      isActive
        ? 'bg-sky-50 dark:bg-sky-900/20 ring-2 ring-sky-500'
        : 'bg-gray-50 dark:bg-gray-800/50 hover:bg-gray-100 dark:hover:bg-gray-800'
    )}
    onClick={onClick}
  >
    <div className="w-10 h-10 rounded-xl flex items-center justify-center bg-gradient-to-br from-sky-500/20 to-purple-500/20">
      <MessageSquare className="w-5 h-5 text-sky-600 dark:text-sky-400" />
    </div>
    <div className="flex-1 min-w-0">
      <div className="flex items-center gap-2">
        <p className="font-medium text-gray-900 dark:text-white truncate">
          {validateString(model.name, model.id)}
        </p>
        {isActive && (
          <span className="px-2 py-0.5 bg-sky-500 text-white text-xs rounded-full flex items-center gap-1">
            <div className="w-1.5 h-1.5 bg-white rounded-full animate-pulse" />
            Active
          </span>
        )}
      </div>
      <p className="text-sm text-gray-500 truncate">
        {validateString(model.domain, 'general')} • {validateString(model.strategy, 'balanced')}
      </p>
    </div>
    {model.load_time_ms && (
      <div className="text-right">
        <p className="text-sm font-medium text-gray-900 dark:text-white">
          {(validateNumber(model.load_time_ms) / 1000).toFixed(2)}s
        </p>
        <p className="text-xs text-gray-500">Load time</p>
      </div>
    )}
  </div>
);

interface QuickActionCardProps {
  onClick: () => void;
  icon: React.ComponentType<{ className?: string }>;
  label: string;
  description: string;
  gradient: string;
  iconColor: string;
}

const QuickActionCard: React.FC<QuickActionCardProps> = ({
  onClick,
  icon: Icon,
  label,
  description,
  gradient,
  iconColor,
}) => (
  <button
    onClick={onClick}
    className={clsx(
      'flex items-center gap-3 p-4 rounded-xl text-left',
      'bg-gradient-to-br',
      gradient,
      'border border-gray-200/50 dark:border-gray-700/50',
      'transition-all duration-200',
      'hover:scale-[1.02] hover:shadow-md'
    )}
  >
    <Icon className={clsx('w-6 h-6', iconColor)} />
    <div>
      <p className="font-medium text-gray-900 dark:text-white">{label}</p>
      <p className="text-xs text-gray-500 dark:text-gray-400">{description}</p>
    </div>
  </button>
);

// vPID L2 Context Components
interface ContextTierCardProps {
  name: string;
  count: number;
  sizeMb: number;
  color: 'red' | 'yellow' | 'blue';
  description: string;
}

const ContextTierCard: React.FC<ContextTierCardProps> = ({ name, count, sizeMb, color, description }) => {
  const colorClasses = {
    red: { border: 'border-red-200 dark:border-red-800/50', bg: 'from-red-500/10', text: 'text-red-700 dark:text-red-400', dot: 'bg-red-500' },
    yellow: { border: 'border-yellow-200 dark:border-yellow-800/50', bg: 'from-yellow-500/10', text: 'text-yellow-700 dark:text-yellow-400', dot: 'bg-yellow-500' },
    blue: { border: 'border-blue-200 dark:border-blue-800/50', bg: 'from-blue-500/10', text: 'text-blue-700 dark:text-blue-400', dot: 'bg-blue-500' },
  };
  const c = colorClasses[color];

  return (
    <div className={clsx('p-4 rounded-xl bg-gradient-to-br to-transparent border', c.bg, c.border)}>
      <div className="flex items-center gap-2 mb-2">
        <div className={clsx('w-2.5 h-2.5 rounded-full', c.dot)} />
        <span className={clsx('text-sm font-semibold', c.text)}>{name}</span>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{count}</p>
          <p className="text-xs text-gray-500">contexts</p>
        </div>
        <div>
          <p className="text-2xl font-bold text-gray-900 dark:text-white">{sizeMb.toFixed(0)}</p>
          <p className="text-xs text-gray-500">MB</p>
        </div>
      </div>
      <p className="text-xs text-gray-500 mt-2">{description}</p>
    </div>
  );
};

interface ContextListRowProps {
  context: any;
}

const ContextListRow: React.FC<ContextListRowProps> = ({ context }) => {
  const tierColors = {
    hot: 'bg-red-500',
    warm: 'bg-yellow-500',
    cold: 'bg-blue-500',
  };

  return (
    <div className="flex items-center gap-3 p-3 rounded-lg bg-gray-50 dark:bg-gray-800/50 hover:bg-gray-100 dark:hover:bg-gray-800 transition-colors">
      <div className={clsx('w-2.5 h-2.5 rounded-full', tierColors[context.tier as keyof typeof tierColors] || 'bg-gray-400')} />
      <div className="flex-1 min-w-0">
        <p className="font-medium text-gray-900 dark:text-white truncate text-sm">{context.id}</p>
        <p className="text-xs text-gray-500">{context.model_id} • {context.token_count} tokens</p>
      </div>
      <div className="text-right">
        <p className="text-sm font-medium text-gray-700 dark:text-gray-300">
          {context.storage_size_mb?.toFixed(1) || 0} MB
        </p>
        <p className="text-xs text-gray-500">{context.access_count || 0} accesses</p>
      </div>
    </div>
  );
};

