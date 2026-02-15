// ============================================================================
// SnapLLM - Quick Switch
// Ultra-Fast Model Switching with vPID Architecture
// ============================================================================

import React, { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Zap,
  Cpu,
  Clock,
  ArrowRight,
  CheckCircle2,
  Layers,
  Gauge,
  Timer,
  Activity,
  TrendingUp,
  Star,
  RefreshCw,
  Play,
  BarChart3,
} from 'lucide-react';
import { listModels, switchModel, getHealth, getMetrics } from '../lib/api';
import { useModelStore } from '../store';
import { Button, IconButton, Badge, Card, Progress } from '../components/ui';

export default function QuickSwitch() {
  const { models, activeModelId, setActiveModel } = useModelStore();
  const [lastSwitchTime, setLastSwitchTime] = useState<number | null>(null);
  const [switchHistory, setSwitchHistory] = useState<Array<{ from: string; to: string; time: number; timestamp: Date }>>([]);
  const [isSwitching, setIsSwitching] = useState(false);

  const { data: modelsResponse } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
  });

  const { data: healthData } = useQuery({
    queryKey: ['health'],
    queryFn: getHealth,
    refetchInterval: 10000,
  });

  const { data: metricsData } = useQuery({
    queryKey: ['metrics'],
    queryFn: getMetrics,
    refetchInterval: 5000,
  });

  // Calculate availability from metrics (successful requests / total requests)
  const totalRequests = metricsData?.total_requests || 0;
  const totalErrors = metricsData?.total_errors || 0;
  const availability = totalRequests > 0
    ? (((totalRequests - totalErrors) / totalRequests) * 100).toFixed(1)
    : healthData?.status === 'ok' ? '100.0' : '0.0';

  // Only show LLM models - diffusion/vision models can't be switched via vPID
  const loadedModels = modelsResponse?.models?.filter((m: any) =>
    m.status === 'loaded' && (m.type === 'llm' || !m.type)
  ) || [];
  const activeModel = loadedModels.find((m: any) => m.id === activeModelId);

  const handleSwitch = async (modelId: string) => {
    if (modelId === activeModelId || isSwitching) return;

    const previousModel = activeModelId;
    setIsSwitching(true);
    const startTime = performance.now();

    try {
      // Call the actual switchModel API
      const response = await switchModel({ name: modelId });

      // Use server-reported switch time if available, otherwise calculate locally
      const switchTime = response.switch_time_ms ?? (performance.now() - startTime);
      setLastSwitchTime(switchTime);
      setActiveModel(modelId);

      if (previousModel) {
        setSwitchHistory(prev => [{
          from: previousModel,
          to: modelId,
          time: switchTime,
          timestamp: new Date(),
        }, ...prev].slice(0, 10));
      }
    } catch (error) {
      console.error('[QuickSwitch] Failed to switch model:', error);
    } finally {
      setIsSwitching(false);
    }
  };

  const avgSwitchTime = switchHistory.length > 0
    ? switchHistory.reduce((sum, h) => sum + h.time, 0) / switchHistory.length
    : 0;

  return (
    <div className="space-y-6">
      {/* Hero Banner */}
      <div className="relative overflow-hidden rounded-2xl bg-gradient-to-r from-sky-600 to-sky-700 dark:from-sky-700 dark:to-sky-800 p-8">
        <div className="absolute inset-0 bg-[url('data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iNjAiIGhlaWdodD0iNjAiIHZpZXdCb3g9IjAgMCA2MCA2MCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj48ZyBmaWxsPSJub25lIiBmaWxsLXJ1bGU9ImV2ZW5vZGQiPjxwYXRoIGQ9Ik0zNiAxOGMtOS45NDEgMC0xOCA4LjA1OS0xOCAxOHM4LjA1OSAxOCAxOCAxOCAxOC04LjA1OSAxOC0xOC04LjA1OS0xOC0xOC0xOHptMCAzMmMtNy43MzIgMC0xNC02LjI2OC0xNC0xNHM2LjI2OC0xNCAxNC0xNCAxNCA2LjI2OCAxNCAxNC02LjI2OCAxNC0xNCAxNHoiIGZpbGw9IiNmZmYiIGZpbGwtb3BhY2l0eT0iLjA1Ii8+PC9nPjwvc3ZnPg==')] opacity-30" />
        <div className="relative flex items-center justify-between">
          <div className="flex items-center gap-6">
            <div className="w-20 h-20 rounded-2xl bg-white/20 backdrop-blur-sm flex items-center justify-center">
              <Zap className="w-10 h-10 text-white" />
            </div>
            <div>
              <h1 className="text-3xl font-bold text-white mb-2">
                Quick Switch
              </h1>
              <p className="text-sky-100 max-w-lg">
                Ultra-fast model switching powered by vPID (Virtual Processing-In-Disk) architecture.
                Switch between loaded models in under 1ms.
              </p>
            </div>
          </div>

          <div className="text-right">
            <div className="text-5xl font-bold text-white mb-1">
              {lastSwitchTime !== null ? `${lastSwitchTime.toFixed(2)}` : '< 1'}
              <span className="text-2xl ml-1">ms</span>
            </div>
            <p className="text-sky-200 text-sm">Last switch time</p>
          </div>
        </div>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Layers className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {loadedModels.length}
              </p>
              <p className="text-sm text-surface-500">Models Loaded</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <Timer className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {avgSwitchTime > 0 ? `${avgSwitchTime.toFixed(2)}ms` : '< 1ms'}
              </p>
              <p className="text-sm text-surface-500">Avg Switch Time</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Activity className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {switchHistory.length}
              </p>
              <p className="text-sm text-surface-500">Switches Today</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
              <Gauge className="w-5 h-5 text-ai-purple" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {availability}%
              </p>
              <p className="text-sm text-surface-500">Availability</p>
            </div>
          </div>
        </Card>
      </div>

      <div className="grid grid-cols-3 gap-6">
        {/* Model Switcher */}
        <div className="col-span-2">
          <Card className="p-6">
            <div className="flex items-center justify-between mb-6">
              <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
                Available Models
              </h2>
              <Badge variant="success" dot>
                {loadedModels.length} ready
              </Badge>
            </div>

            <div className="space-y-3">
              {loadedModels.map((model: any) => {
                const isActive = model.id === activeModelId;
                return (
                  <motion.button
                    key={model.id}
                    onClick={() => handleSwitch(model.id)}
                    disabled={isSwitching}
                    whileHover={{ scale: 1.01 }}
                    whileTap={{ scale: 0.99 }}
                    className={clsx(
                      'w-full flex items-center gap-4 p-4 rounded-xl border-2 transition-all',
                      isActive
                        ? 'border-brand-500 bg-brand-50 dark:bg-brand-900/20'
                        : 'border-surface-200 dark:border-surface-700 hover:border-brand-300 dark:hover:border-brand-700'
                    )}
                  >
                    <div className={clsx(
                      'w-12 h-12 rounded-xl flex items-center justify-center',
                      isActive
                        ? 'bg-brand-500'
                        : 'bg-surface-100 dark:bg-surface-800'
                    )}>
                      <Cpu className={clsx(
                        'w-6 h-6',
                        isActive ? 'text-white' : 'text-surface-500'
                      )} />
                    </div>

                    <div className="flex-1 text-left">
                      <div className="flex items-center gap-2">
                        <p className={clsx(
                          'text-base font-semibold',
                          isActive ? 'text-brand-700 dark:text-brand-300' : 'text-surface-900 dark:text-white'
                        )}>
                          {model.name}
                        </p>
                        {isActive && (
                          <Badge variant="success" size="sm">Active</Badge>
                        )}
                      </div>
                      <p className="text-sm text-surface-500">
                        {model.type?.toUpperCase()} • {model.quantization || 'N/A'} • {model.parameters || 'N/A'}
                      </p>
                    </div>

                    <div className="flex items-center gap-4">
                      <div className="text-right">
                        <p className="text-sm font-medium text-surface-900 dark:text-white">
                          {model.performance?.tokensPerSecond?.toFixed(0) || '—'} tok/s
                        </p>
                        <p className="text-xs text-surface-500">Performance</p>
                      </div>

                      {!isActive && (
                        <div className="flex items-center gap-2 text-brand-600 dark:text-brand-400">
                          <span className="text-sm font-medium">Switch</span>
                          <ArrowRight className="w-4 h-4" />
                        </div>
                      )}

                      {isActive && (
                        <CheckCircle2 className="w-6 h-6 text-success-500" />
                      )}
                    </div>
                  </motion.button>
                );
              })}

              {loadedModels.length === 0 && (
                <div className="text-center py-12 text-surface-500">
                  <Cpu className="w-12 h-12 mx-auto mb-3 opacity-50" />
                  <p>No models loaded</p>
                  <p className="text-sm">Load models from the Model Hub</p>
                </div>
              )}
            </div>
          </Card>
        </div>

        {/* Switch History */}
        <div>
          <Card className="p-6">
            <h2 className="text-lg font-semibold text-surface-900 dark:text-white mb-4">
              Switch History
            </h2>

            <div className="space-y-3">
              {switchHistory.length > 0 ? (
                switchHistory.map((entry, i) => {
                  const fromModel = loadedModels.find((m: any) => m.id === entry.from);
                  const toModel = loadedModels.find((m: any) => m.id === entry.to);
                  return (
                    <div
                      key={i}
                      className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
                    >
                      <div className="flex items-center gap-2 mb-1">
                        <span className="text-sm text-surface-600 dark:text-surface-400 truncate">
                          {fromModel?.name || entry.from}
                        </span>
                        <ArrowRight className="w-3 h-3 text-surface-400 flex-shrink-0" />
                        <span className="text-sm font-medium text-surface-900 dark:text-white truncate">
                          {toModel?.name || entry.to}
                        </span>
                      </div>
                      <div className="flex items-center justify-between text-xs text-surface-500">
                        <span className="flex items-center gap-1">
                          <Zap className="w-3 h-3 text-warning-500" />
                          {entry.time.toFixed(2)}ms
                        </span>
                        <span>{entry.timestamp.toLocaleTimeString()}</span>
                      </div>
                    </div>
                  );
                })
              ) : (
                <div className="text-center py-8 text-surface-500">
                  <Clock className="w-8 h-8 mx-auto mb-2 opacity-50" />
                  <p className="text-sm">No switches yet</p>
                </div>
              )}
            </div>
          </Card>

          {/* vPID Info */}
          <Card className="p-6 mt-4 bg-gradient-to-br from-brand-500/10 to-ai-purple/10 border-brand-200 dark:border-brand-800">
            <div className="flex items-start gap-3">
              <div className="w-10 h-10 rounded-lg bg-brand-500 flex items-center justify-center flex-shrink-0">
                <Zap className="w-5 h-5 text-white" />
              </div>
              <div>
                <h3 className="font-semibold text-surface-900 dark:text-white mb-1">
                  vPID Technology
                </h3>
                <p className="text-sm text-surface-600 dark:text-surface-400">
                  Virtual Processing-In-Disk architecture enables instant model switching by keeping
                  models in a 3-tier cache (HOT/WARM/COLD) with zero context loss.
                </p>
              </div>
            </div>
          </Card>
        </div>
      </div>
    </div>
  );
}

