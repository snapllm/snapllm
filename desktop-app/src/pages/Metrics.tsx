// ============================================================================
// SnapLLM Enterprise - Analytics & Observability Dashboard
// Real-time Metrics, Performance Monitoring, and System Insights
// ============================================================================

import React, { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { motion } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Activity,
  Clock,
  Zap,
  TrendingUp,
  BarChart3,
  Cpu,
  MemoryStick,
  HardDrive,
  Server,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  RefreshCw,
  Download,
  Layers,
  Database,
  Filter,
} from 'lucide-react';
import { listModels, getServerStatus, getMetrics, getCacheStats } from '../lib/api';
import { Button, IconButton, Badge, Card, Progress } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface MetricCard {
  title: string;
  value: string | number;
  changeLabel?: string;
  icon: React.ComponentType<{ className?: string }>;
  color: 'brand' | 'success' | 'warning' | 'error' | 'purple';
}

interface UsageByType {
  type: string;
  count: number;
  percentage: number;
  color: string;
}

// ============================================================================
// Default Values - replaced by real API data when available
// ============================================================================

// Default values - will be replaced by real API data in component
const DEFAULT_USAGE_BY_TYPE: UsageByType[] = [
  { type: 'Chat Completions', count: 0, percentage: 100, color: '#6366f1' },
];

const DEFAULT_TOP_MODELS = [
  { name: 'No models loaded', requests: 0, avgLatency: 0, throughput: 0 },
];

const DEFAULT_ALERTS = [
  { id: '1', type: 'info', message: 'Connect to server to see alerts', time: 'now' },
];

// ============================================================================
// Chart Components
// ============================================================================

function UsageDonutChart({ data }: { data: UsageByType[] }) {
  const total = data.reduce((sum, d) => sum + (d.count || 0), 0);
  let currentAngle = 0;

  return (
    <div className="relative w-32 h-32">
      <svg viewBox="0 0 100 100" className="w-full h-full transform -rotate-90">
        {data.map((item, i) => {
          const angle = (item.count / total) * 360;
          const startAngle = currentAngle;
          currentAngle += angle;

          const startRad = (startAngle * Math.PI) / 180;
          const endRad = ((startAngle + angle) * Math.PI) / 180;
          const largeArc = angle > 180 ? 1 : 0;

          const x1 = 50 + 40 * Math.cos(startRad);
          const y1 = 50 + 40 * Math.sin(startRad);
          const x2 = 50 + 40 * Math.cos(endRad);
          const y2 = 50 + 40 * Math.sin(endRad);

          const path = `M 50 50 L ${x1} ${y1} A 40 40 0 ${largeArc} 1 ${x2} ${y2} Z`;

          return (
            <path
              key={i}
              d={path}
              fill={item.color}
              className="hover:opacity-80 transition-opacity cursor-pointer"
            />
          );
        })}
        <circle cx="50" cy="50" r="25" className="fill-white dark:fill-surface-900" />
      </svg>
      <div className="absolute inset-0 flex items-center justify-center">
        <div className="text-center">
          <p className="text-lg font-bold text-surface-900 dark:text-white">
            {((total || 0) / 1000).toFixed(0)}K
          </p>
          <p className="text-xs text-surface-500">Total</p>
        </div>
      </div>
    </div>
  );
}

// ============================================================================
// Main Component
// ============================================================================

export default function Metrics() {
  const [timeRange, setTimeRange] = useState<'1h' | '24h' | '7d' | '30d'>('24h');

  const { data: status } = useQuery({
    queryKey: ['server-status'],
    queryFn: getServerStatus,
    refetchInterval: 5000,
  });

  const { data: modelsResponse } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 10000,
  });

  const { data: metricsData } = useQuery({
    queryKey: ['server-metrics'],
    queryFn: getMetrics,
    refetchInterval: 5000,
  });

  const { data: cacheStats } = useQuery({
    queryKey: ['cache-stats'],
    queryFn: getCacheStats,
    refetchInterval: 5000,
  });

  const models = modelsResponse?.models || [];

  // Compute system resource metrics from cache stats
  const systemMetrics = React.useMemo(() => {
    if (!cacheStats?.models || cacheStats.models.length === 0) {
      return { gpuUsage: 0, cacheUsage: 0, workspaceUsage: 0, workspaceTotal: 0, cacheTotal: 0 };
    }
    const currentModel = cacheStats.models.find((m: any) => m.is_current) || cacheStats.models[0];
    return {
      gpuUsage: currentModel.tensor_cache_utilization || 0,
      cacheUsage: currentModel.tensor_cache_used_mb || 0,
      cacheTotal: currentModel.tensor_cache_budget_mb || 4096,
      workspaceUsage: currentModel.workspace_used_mb || 0,
      workspaceTotal: currentModel.workspace_total_mb || 0,
      hitRate: (cacheStats.summary?.global_hit_rate || 0) * 100,
    };
  }, [cacheStats]);
  
  // Build real data from API metrics
  const USAGE_BY_TYPE = React.useMemo(() => {
    if (metricsData?.models && metricsData.models.length > 0) {
      const colors = ['#6366f1', '#8b5cf6', '#a855f7', '#c084fc', '#e879f9'];
      const total = metricsData.total_requests || 1;
      return metricsData.models.map((m: any, i: number) => ({
        type: m.model_name || 'Model',
        count: m.requests || 0,
        percentage: total > 0 ? Math.round(((m.requests || 0) / total) * 100) : 0,
        color: colors[i % colors.length],
      }));
    }
    return DEFAULT_USAGE_BY_TYPE;
  }, [metricsData]);
  
  const TOP_MODELS = React.useMemo(() => {
    if (metricsData?.models && metricsData.models.length > 0) {
      return metricsData.models.map((m: any) => ({
        name: m.model_name || 'Unknown',
        requests: m.requests || 0,
        avgLatency: m.avg_latency_ms || 0,
        throughput: m.tokens_per_second || 0,
      })).sort((a: any, b: any) => b.requests - a.requests);
    }
    return DEFAULT_TOP_MODELS;
  }, [metricsData]);
  
  const RECENT_ALERTS = React.useMemo(() => {
    // Generate alerts from current system state
    const alerts: Array<{id: string, type: string, message: string, time: string}> = [];
    if (status?.system?.memory?.percent > 80) {
      alerts.push({ id: '1', type: 'warning', message: `High memory usage (${status.system.memory.percent}%)`, time: 'now' });
    }
    if (status?.system?.cpu_percent > 80) {
      alerts.push({ id: '2', type: 'warning', message: `High CPU usage (${status.system.cpu_percent}%)`, time: 'now' });
    }
    if (models.length > 0) {
      alerts.push({ id: '3', type: 'success', message: `${models.length} model(s) loaded successfully`, time: 'recent' });
    }
    if (metricsData?.total_requests > 0) {
      alerts.push({ id: '4', type: 'info', message: `${metricsData.total_requests} total requests processed`, time: 'today' });
    }
    return alerts.length > 0 ? alerts : DEFAULT_ALERTS;
  }, [status, models, metricsData]);

  const formatUptime = (seconds: number) => {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (days > 0) return `${days}d ${hours}h ${minutes}m`;
    if (hours > 0) return `${hours}h ${minutes}m`;
    return `${minutes}m`;
  };

  const totalRequests = models?.reduce((acc, m) => acc + (m.requests_per_hour || 0), 0) || 0;
  const avgLatency = models && models.length > 0
    ? models.reduce((acc, m) => acc + (m.avg_latency_ms || 0), 0) / models.length
    : 0;
  const totalThroughput = models?.reduce((acc, m) => acc + (m.throughput_toks || 0), 0) || 0;
  const totalMemory = models?.reduce((acc, m) => acc + (m.ram_usage_mb || 0), 0) || 0;

  const metrics: MetricCard[] = [
    {
      title: 'Server Uptime',
      value: status ? formatUptime(status.uptime_seconds) : '-',
      changeLabel: `v${status?.version || '-'}`,
      icon: Clock,
      color: 'brand',
    },
    {
      title: 'Requests/Hour',
      value: totalRequests.toLocaleString(),
      changeLabel: `${models.length} models`,
      icon: Activity,
      color: 'success',
    },
    {
      title: 'Avg Latency',
      value: avgLatency > 0 ? `${avgLatency.toFixed(0)}ms` : '-',
      changeLabel: models.length > 0 ? 'across all models' : 'no models',
      icon: Zap,
      color: 'warning',
    },
    {
      title: 'Throughput',
      value: totalThroughput > 0 ? `${totalThroughput.toFixed(1)} tok/s` : '-',
      changeLabel: models.length > 0 ? 'combined' : 'no models',
      icon: TrendingUp,
      color: 'purple',
    },
  ];

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Analytics & Observability
          </h1>
          <p className="text-surface-500 mt-1">
            Real-time performance monitoring and system insights
          </p>
        </div>
        <div className="flex items-center gap-2">
          {/* Time Range Selector */}
          <div className="flex items-center gap-1 bg-surface-100 dark:bg-surface-800 rounded-lg p-1">
            {(['1h', '24h', '7d', '30d'] as const).map(range => (
              <button
                key={range}
                onClick={() => setTimeRange(range)}
                className={clsx(
                  'px-3 py-1.5 rounded-md text-sm font-medium transition-colors',
                  timeRange === range
                    ? 'bg-white dark:bg-surface-700 text-surface-900 dark:text-white shadow-sm'
                    : 'text-surface-500 hover:text-surface-700 dark:hover:text-surface-300'
                )}
              >
                {range}
              </button>
            ))}
          </div>
          <Button variant="secondary">
            <RefreshCw className="w-4 h-4" />
            Refresh
          </Button>
          <Button variant="secondary">
            <Download className="w-4 h-4" />
            Export
          </Button>
        </div>
      </div>

      {/* Top Metrics */}
      <div className="grid grid-cols-4 gap-4">
        {metrics.map((metric, i) => (
          <motion.div
            key={metric.title}
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ delay: i * 0.1 }}
          >
            <Card className="p-4">
              <div className="flex items-center gap-3">
                <div className={clsx(
                  'w-10 h-10 rounded-lg flex items-center justify-center',
                  metric.color === 'brand' && 'bg-brand-100 dark:bg-brand-900/30',
                  metric.color === 'success' && 'bg-success-100 dark:bg-success-900/30',
                  metric.color === 'warning' && 'bg-warning-100 dark:bg-warning-900/30',
                  metric.color === 'error' && 'bg-error-100 dark:bg-error-900/30',
                  metric.color === 'purple' && 'bg-ai-purple/20'
                )}>
                  <metric.icon className={clsx(
                    'w-5 h-5',
                    metric.color === 'brand' && 'text-brand-600 dark:text-brand-400',
                    metric.color === 'success' && 'text-success-600 dark:text-success-400',
                    metric.color === 'warning' && 'text-warning-600 dark:text-warning-400',
                    metric.color === 'error' && 'text-error-600 dark:text-error-400',
                    metric.color === 'purple' && 'text-ai-purple'
                  )} />
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {metric.value}
                  </p>
                  <p className="text-sm text-surface-500">{metric.title}</p>
                  {metric.changeLabel && (
                    <p className="text-xs text-surface-400">{metric.changeLabel}</p>
                  )}
                </div>
              </div>
            </Card>
          </motion.div>
        ))}
      </div>

      {/* Performance Overview Row */}
      <div className="grid grid-cols-2 gap-6">
        {/* Throughput Summary */}
        <Card className="p-6">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="font-semibold text-surface-900 dark:text-white">Throughput Summary</h3>
              <p className="text-sm text-surface-500">Tokens per second by model</p>
            </div>
            <Badge variant={totalThroughput > 0 ? 'success' : 'default'}>
              {totalThroughput > 0 ? `${totalThroughput.toFixed(1)} tok/s` : 'No data'}
            </Badge>
          </div>
          <div className="space-y-3">
            {models.length > 0 ? models.map(model => (
              <div key={model.id} className="flex items-center justify-between">
                <span className="text-sm text-surface-600 dark:text-surface-400 truncate flex-1">
                  {model.name}
                </span>
                <div className="flex items-center gap-2">
                  <Progress
                    value={totalThroughput > 0 ? ((model.throughput_toks || 0) / totalThroughput) * 100 : 0}
                    variant="brand"
                    className="w-24"
                  />
                  <span className="text-sm font-medium text-surface-900 dark:text-white w-20 text-right">
                    {(model.throughput_toks || 0).toFixed(1)} tok/s
                  </span>
                </div>
              </div>
            )) : (
              <div className="text-center py-8 text-surface-500">
                <TrendingUp className="w-8 h-8 mx-auto mb-2 opacity-50" />
                <p className="text-sm">Load models to see throughput</p>
              </div>
            )}
          </div>
        </Card>

        {/* Latency Summary */}
        <Card className="p-6">
          <div className="flex items-center justify-between mb-4">
            <div>
              <h3 className="font-semibold text-surface-900 dark:text-white">Latency Summary</h3>
              <p className="text-sm text-surface-500">Average response time by model</p>
            </div>
            <Badge variant={avgLatency > 0 ? (avgLatency < 500 ? 'success' : 'warning') : 'default'}>
              {avgLatency > 0 ? `${avgLatency.toFixed(0)}ms avg` : 'No data'}
            </Badge>
          </div>
          <div className="space-y-3">
            {models.length > 0 ? models.map(model => (
              <div key={model.id} className="flex items-center justify-between">
                <span className="text-sm text-surface-600 dark:text-surface-400 truncate flex-1">
                  {model.name}
                </span>
                <div className="flex items-center gap-2">
                  <Progress
                    value={Math.min(((model.avg_latency_ms || 0) / 2000) * 100, 100)}
                    variant={(model.avg_latency_ms || 0) < 500 ? 'success' : 'warning'}
                    className="w-24"
                  />
                  <span className="text-sm font-medium text-surface-900 dark:text-white w-20 text-right">
                    {(model.avg_latency_ms || 0).toFixed(0)} ms
                  </span>
                </div>
              </div>
            )) : (
              <div className="text-center py-8 text-surface-500">
                <Zap className="w-8 h-8 mx-auto mb-2 opacity-50" />
                <p className="text-sm">Load models to see latency</p>
              </div>
            )}
          </div>
        </Card>
      </div>

      {/* Middle Row */}
      <div className="grid grid-cols-3 gap-6">
        {/* Usage by Type */}
        <Card className="p-6">
          <h3 className="font-semibold text-surface-900 dark:text-white mb-4">Usage by Type</h3>
          <div className="flex items-center gap-6">
            <UsageDonutChart data={USAGE_BY_TYPE} />
            <div className="flex-1 space-y-2">
              {USAGE_BY_TYPE.map(item => (
                <div key={item.type} className="flex items-center gap-2">
                  <div
                    className="w-3 h-3 rounded-full"
                    style={{ backgroundColor: item.color }}
                  />
                  <span className="text-xs text-surface-600 dark:text-surface-400 flex-1 truncate">
                    {item.type}
                  </span>
                  <span className="text-xs font-medium text-surface-900 dark:text-white">
                    {item.percentage}%
                  </span>
                </div>
              ))}
            </div>
          </div>
        </Card>

        {/* Top Models */}
        <Card className="p-6 col-span-2">
          <div className="flex items-center justify-between mb-4">
            <h3 className="font-semibold text-surface-900 dark:text-white">Top Models by Usage</h3>
            <Button variant="ghost" size="sm">View All</Button>
          </div>
          <div className="space-y-3">
            {TOP_MODELS.map((model, i) => (
              <div
                key={model.name}
                className="flex items-center gap-4 p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
              >
                <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center text-white text-sm font-bold">
                  {i + 1}
                </div>
                <div className="flex-1 min-w-0">
                  <p className="font-medium text-surface-900 dark:text-white truncate">
                    {model.name}
                  </p>
                  <p className="text-xs text-surface-500">
                    {model.requests.toLocaleString()} requests
                  </p>
                </div>
                <div className="text-right">
                  <p className="text-sm font-medium text-surface-900 dark:text-white">
                    {model.avgLatency}ms
                  </p>
                  <p className="text-xs text-surface-500">avg latency</p>
                </div>
                <div className="text-right">
                  <p className="text-sm font-medium text-success-600 dark:text-success-400">
                    {model.throughput} tok/s
                  </p>
                  <p className="text-xs text-surface-500">throughput</p>
                </div>
              </div>
            ))}
          </div>
        </Card>
      </div>

      {/* System Health & Alerts */}
      <div className="grid grid-cols-3 gap-6">
        {/* System Resources */}
        <Card className="p-6 col-span-2">
          <h3 className="font-semibold text-surface-900 dark:text-white mb-4">System Resources</h3>
          <div className="grid grid-cols-2 gap-6">
            {/* Memory */}
            <div>
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <MemoryStick className="w-4 h-4 text-brand-500" />
                  <span className="text-sm text-surface-600 dark:text-surface-400">Model Memory</span>
                </div>
                <span className="text-sm font-semibold text-surface-900 dark:text-white">
                  {((totalMemory || 0) / 1024).toFixed(2)} GB
                </span>
              </div>
              <Progress value={Math.min(((totalMemory || 0) / 1024 / 16) * 100, 100)} variant="brand" />
              <p className="text-xs text-surface-500 mt-2">
                {models.length} model{models.length !== 1 ? 's' : ''} loaded
              </p>
            </div>

            {/* GPU / Tensor Cache */}
            <div>
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <Zap className="w-4 h-4 text-ai-purple" />
                  <span className="text-sm text-surface-600 dark:text-surface-400">Tensor Cache</span>
                </div>
                <span className="text-sm font-semibold text-surface-900 dark:text-white">
                  {systemMetrics.cacheUsage.toFixed(0)} / {systemMetrics.cacheTotal} MB
                </span>
              </div>
              <Progress value={(systemMetrics.cacheUsage / systemMetrics.cacheTotal) * 100 || 0} variant="brand" />
              <p className="text-xs text-surface-500 mt-2">
                vPID Tensor Cache • {systemMetrics.gpuUsage.toFixed(1)}% utilized
              </p>
            </div>

            {/* Cache Hit Rate */}
            <div>
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <Cpu className="w-4 h-4 text-success-500" />
                  <span className="text-sm text-surface-600 dark:text-surface-400">Cache Hit Rate</span>
                </div>
                <span className="text-sm font-semibold text-surface-900 dark:text-white">
                  {systemMetrics.hitRate?.toFixed(1) || 0}%
                </span>
              </div>
              <Progress value={systemMetrics.hitRate || 0} variant="success" />
              <p className="text-xs text-surface-500 mt-2">
                {cacheStats?.summary?.total_cache_hits || 0} hits / {(cacheStats?.summary?.total_cache_hits || 0) + (cacheStats?.summary?.total_cache_misses || 0)} total
              </p>
            </div>

            {/* Workspace Storage */}
            <div>
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <HardDrive className="w-4 h-4 text-warning-500" />
                  <span className="text-sm text-surface-600 dark:text-surface-400">Workspace</span>
                </div>
                <span className="text-sm font-semibold text-surface-900 dark:text-white">
                  {systemMetrics.workspaceUsage.toFixed(0)} / {(systemMetrics.workspaceTotal / 1024).toFixed(1)} GB
                </span>
              </div>
              <Progress value={systemMetrics.workspaceTotal > 0 ? (systemMetrics.workspaceUsage / systemMetrics.workspaceTotal) * 100 : 0} variant="warning" />
              <p className="text-xs text-surface-500 mt-2">
                Models: {models.length} loaded • Cache: {systemMetrics.cacheUsage.toFixed(0)} MB
              </p>
            </div>
          </div>
        </Card>

        {/* Recent Alerts */}
        <Card className="p-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="font-semibold text-surface-900 dark:text-white">System Status</h3>
            <Badge variant={RECENT_ALERTS.some(a => a.type === 'error') ? 'error' : RECENT_ALERTS.some(a => a.type === 'warning') ? 'warning' : 'success'}>
              {RECENT_ALERTS.length} item{RECENT_ALERTS.length !== 1 ? 's' : ''}
            </Badge>
          </div>
          <div className="space-y-3">
            {RECENT_ALERTS.map(alert => (
              <div
                key={alert.id}
                className={clsx(
                  'p-3 rounded-lg border',
                  alert.type === 'error' && 'bg-error-50 dark:bg-error-900/20 border-error-200 dark:border-error-800',
                  alert.type === 'warning' && 'bg-warning-50 dark:bg-warning-900/20 border-warning-200 dark:border-warning-800',
                  alert.type === 'success' && 'bg-success-50 dark:bg-success-900/20 border-success-200 dark:border-success-800',
                  alert.type === 'info' && 'bg-brand-50 dark:bg-brand-900/20 border-brand-200 dark:border-brand-800'
                )}
              >
                <div className="flex items-start gap-2">
                  {alert.type === 'error' && <XCircle className="w-4 h-4 text-error-500 mt-0.5" />}
                  {alert.type === 'warning' && <AlertTriangle className="w-4 h-4 text-warning-500 mt-0.5" />}
                  {alert.type === 'success' && <CheckCircle2 className="w-4 h-4 text-success-500 mt-0.5" />}
                  {alert.type === 'info' && <Activity className="w-4 h-4 text-brand-500 mt-0.5" />}
                  <div className="flex-1 min-w-0">
                    <p className="text-sm text-surface-700 dark:text-surface-300">
                      {alert.message}
                    </p>
                    <p className="text-xs text-surface-500 mt-1">{alert.time}</p>
                  </div>
                </div>
              </div>
            ))}
          </div>
          <Button variant="ghost" size="sm" className="w-full mt-3">
            View All Alerts
          </Button>
        </Card>
      </div>

      {/* Per-Model Performance */}
      <Card className="p-6">
        <div className="flex items-center justify-between mb-6">
          <div>
            <h3 className="font-semibold text-surface-900 dark:text-white">Model Performance</h3>
            <p className="text-sm text-surface-500">Detailed metrics for loaded models</p>
          </div>
          <div className="flex items-center gap-2">
            <Button variant="secondary" size="sm">
              <Filter className="w-4 h-4 mr-1.5" />
              Filter
            </Button>
            <Button variant="secondary" size="sm">
              <Download className="w-4 h-4 mr-1.5" />
              Export CSV
            </Button>
          </div>
        </div>

        {models && models.length > 0 ? (
          <div className="overflow-x-auto">
            <table className="w-full">
              <thead>
                <tr className="border-b border-surface-200 dark:border-surface-700">
                  <th className="text-left py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Model
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Status
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Memory
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Requests/Hr
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Avg Latency
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Throughput
                  </th>
                  <th className="text-right py-3 px-4 text-xs font-semibold text-surface-500 uppercase tracking-wider">
                    Device
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-surface-200 dark:divide-surface-700">
                {models.map(model => (
                  <tr key={model.id} className="hover:bg-surface-50 dark:hover:bg-surface-800/50">
                    <td className="py-3 px-4">
                      <div className="flex items-center gap-3">
                        <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
                          <Cpu className="w-4 h-4 text-white" />
                        </div>
                        <div>
                          <p className="font-medium text-surface-900 dark:text-white">{model.name}</p>
                          <p className="text-xs text-surface-500">{model.engine}</p>
                        </div>
                      </div>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <Badge variant="success" size="sm">
                        <span className="w-1.5 h-1.5 rounded-full bg-current mr-1" />
                        {model.status}
                      </Badge>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span className="font-medium text-surface-900 dark:text-white">
                        {((model.ram_usage_mb || 0) / 1024).toFixed(2)} GB
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span className="font-medium text-surface-900 dark:text-white">
                        {model.requests_per_hour || 0}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span className="font-medium text-surface-900 dark:text-white">
                        {(model.avg_latency_ms || 0).toFixed(0)} ms
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <span className="font-medium text-success-600 dark:text-success-400">
                        {(model.throughput_toks || 0).toFixed(1)} tok/s
                      </span>
                    </td>
                    <td className="py-3 px-4 text-right">
                      <Badge variant={model.device === 'CUDA' ? 'brand' : 'default'} size="sm">
                        {model.device === 'CUDA' && <Zap className="w-3 h-3" />}
                        {model.device}
                      </Badge>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="text-center py-12">
            <div className="w-16 h-16 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-4">
              <BarChart3 className="w-8 h-8 text-surface-400" />
            </div>
            <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
              No Models Loaded
            </h3>
            <p className="text-surface-500">
              Load models to see performance metrics
            </p>
          </div>
        )}
      </Card>

      {/* System Info Footer */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <Server className="w-5 h-5 text-brand-500" />
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">API Server</p>
              <p className="text-xs text-surface-500">Port {status?.port || '-'}</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <Zap className="w-5 h-5 text-ai-purple" />
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">GPU Enabled</p>
              <p className="text-xs text-surface-500">{status?.gpu_enabled ? 'Yes (CUDA)' : 'No'}</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <Layers className="w-5 h-5 text-success-500" />
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">Models Loaded</p>
              <p className="text-xs text-surface-500">{status?.models_loaded || 0} active</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <Database className="w-5 h-5 text-warning-500" />
            <div>
              <p className="text-sm font-medium text-surface-900 dark:text-white">Cache Status</p>
              <p className="text-xs text-surface-500">{systemMetrics.hitRate?.toFixed(0) || 0}% hit rate</p>
            </div>
          </div>
        </Card>
      </div>
    </div>
  );
}
