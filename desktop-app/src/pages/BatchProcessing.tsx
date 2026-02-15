// ============================================================================
// SnapLLM - Batch Processing
// Process multiple prompts with tool calling support
// ============================================================================

import React, { useState, useCallback } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Layers,
  Play,
  Pause,
  Plus,
  Trash2,
  Copy,
  Check,
  Download,
  Upload,
  Settings2,
  ChevronDown,
  ChevronRight,
  Cpu,
  Zap,
  Clock,
  AlertCircle,
  CheckCircle2,
  Loader2,
  FileText,
  Wrench,
  Code2,
  RefreshCw,
  BarChart3,
  List,
  Grid,
} from 'lucide-react';
import { listModels, generateBatch, BatchGenerateRequest, BatchGenerateResponse, BatchResultItem } from '../lib/api';
import { Button, IconButton, Badge, Card, Toggle } from '../components/ui';
import { MarkdownRenderer } from '../components/ui/MarkdownRenderer';

// ============================================================================
// Types
// ============================================================================

interface BatchItem {
  id: string;
  prompt: string;
  systemPrompt?: string;
  status: 'pending' | 'processing' | 'completed' | 'error';
  result?: string;
  error?: string;
  tokensGenerated?: number;
  generationTime?: number;
}

interface Tool {
  name: string;
  description: string;
  parameters: Record<string, any>;
}

// ============================================================================
// Preset Prompts
// ============================================================================

const PRESET_PROMPTS = [
  { label: 'Summarize', prompt: 'Summarize the following text in 2-3 sentences:' },
  { label: 'Translate', prompt: 'Translate the following to Spanish:' },
  { label: 'Code Review', prompt: 'Review this code and suggest improvements:' },
  { label: 'Extract Data', prompt: 'Extract key information from this text as JSON:' },
  { label: 'Sentiment', prompt: 'Analyze the sentiment of this text (positive/negative/neutral):' },
];

// ============================================================================
// Main Component
// ============================================================================

export default function BatchProcessing() {
  // State
  const [batchItems, setBatchItems] = useState<BatchItem[]>([
    { id: crypto.randomUUID(), prompt: '', status: 'pending' },
  ]);
  const [selectedModel, setSelectedModel] = useState('');
  const [maxTokens, setMaxTokens] = useState(256);
  const [temperature, setTemperature] = useState(0.7);
  const [systemPrompt, setSystemPrompt] = useState('');
  const [showSettings, setShowSettings] = useState(false);
  const [viewMode, setViewMode] = useState<'list' | 'grid'>('list');
  const [copiedId, setCopiedId] = useState<string | null>(null);

  // Tool calling state
  const [enableTools, setEnableTools] = useState(false);
  const [tools, setTools] = useState<Tool[]>([
    {
      name: 'get_weather',
      description: 'Get current weather for a location',
      parameters: { location: { type: 'string', description: 'City name' } },
    },
  ]);

  // Fetch models
  const { data: modelsResponse, isLoading: modelsLoading } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
  });

  const loadedModels = modelsResponse?.models?.filter((m: any) => m.status === 'loaded') || [];

  // Batch mutation
  const batchMutation = useMutation({
    mutationFn: generateBatch,
    onSuccess: (data: BatchGenerateResponse) => {
      setBatchItems(prev => prev.map((item, idx) => {
        const result = data.results[idx];
        if (result) {
          return {
            ...item,
            status: result.success ? 'completed' as const : 'error' as const,
            result: result.success ? result.generated_text : undefined,
            error: result.success ? undefined : (result.error || 'Generation failed'),
            tokensGenerated: result.tokens_generated,
            generationTime: result.latency_ms,
          };
        }
        return item;
      }));
    },
    onError: (error: any) => {
      setBatchItems(prev => prev.map(item => ({
        ...item,
        status: 'error' as const,
        error: error.message || 'Unknown error',
      })));
    },
  });

  // Add new item
  const addItem = () => {
    setBatchItems(prev => [...prev, {
      id: crypto.randomUUID(),
      prompt: '',
      status: 'pending',
    }]);
  };

  // Remove item
  const removeItem = (id: string) => {
    setBatchItems(prev => prev.filter(item => item.id !== id));
  };

  // Update item prompt
  const updatePrompt = (id: string, prompt: string) => {
    setBatchItems(prev => prev.map(item =>
      item.id === id ? { ...item, prompt } : item
    ));
  };

  // Apply preset to all items
  const applyPreset = (preset: string) => {
    setBatchItems(prev => prev.map(item => ({
      ...item,
      prompt: preset + ' ' + item.prompt,
    })));
  };

  // Run batch
  const runBatch = useCallback(() => {
    const activeItems = batchItems.filter(item => item.prompt.trim());
    if (activeItems.length === 0 || !selectedModel) return;

    // Set all items to processing
    setBatchItems(prev => prev.map(item => ({
      ...item,
      status: item.prompt.trim() ? 'processing' : 'pending',
      result: undefined,
      error: undefined,
      tokensGenerated: undefined,
      generationTime: undefined,
    })));

    // Build items with per-prompt system prompts and messages format
    const items = activeItems.map(item => {
      const itemSystemPrompt = item.systemPrompt || systemPrompt;
      if (itemSystemPrompt) {
        return {
          messages: [
            { role: 'system' as const, content: itemSystemPrompt },
            { role: 'user' as const, content: item.prompt },
          ],
          max_tokens: maxTokens,
          temperature,
        };
      }
      return {
        prompt: item.prompt,
        max_tokens: maxTokens,
        temperature,
      };
    });

    batchMutation.mutate({
      items,
      model: selectedModel,
      max_tokens: maxTokens,
      temperature,
    });
  }, [batchItems, selectedModel, maxTokens, temperature, systemPrompt, batchMutation]);

  // Copy result
  const copyResult = (id: string, text: string) => {
    navigator.clipboard.writeText(text);
    setCopiedId(id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  // Export results
  const exportResults = () => {
    const results = batchItems
      .filter(item => item.result)
      .map(item => ({
        prompt: item.prompt,
        system_prompt: item.systemPrompt || systemPrompt || undefined,
        result: item.result,
        tokens_generated: item.tokensGenerated,
        latency_ms: item.generationTime,
      }));

    const blob = new Blob([JSON.stringify(results, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `batch-results-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  // Stats
  const completedCount = batchItems.filter(i => i.status === 'completed').length;
  const errorCount = batchItems.filter(i => i.status === 'error').length;
  const pendingCount = batchItems.filter(i => i.status === 'pending' && i.prompt.trim()).length;

  return (
    <div className="p-6 max-w-7xl mx-auto">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-gradient-to-br from-violet-500 to-purple-600 flex items-center justify-center">
              <Layers className="w-5 h-5 text-white" />
            </div>
            Batch Processing
          </h1>
          <p className="text-surface-500 mt-1">
            Process multiple prompts in parallel with custom system prompts
          </p>
        </div>

        <div className="flex items-center gap-2">
          <div className="flex items-center gap-1 p-1 bg-surface-100 dark:bg-surface-800 rounded-lg">
            <button
              onClick={() => setViewMode('list')}
              className={clsx(
                'p-2 rounded-md transition-colors',
                viewMode === 'list'
                  ? 'bg-white dark:bg-surface-700 shadow-sm'
                  : 'hover:bg-surface-200 dark:hover:bg-surface-700'
              )}
            >
              <List className="w-4 h-4" />
            </button>
            <button
              onClick={() => setViewMode('grid')}
              className={clsx(
                'p-2 rounded-md transition-colors',
                viewMode === 'grid'
                  ? 'bg-white dark:bg-surface-700 shadow-sm'
                  : 'hover:bg-surface-200 dark:hover:bg-surface-700'
              )}
            >
              <Grid className="w-4 h-4" />
            </button>
          </div>
          <IconButton
            icon={<Settings2 className={clsx(showSettings && 'text-brand-500')} />}
            label="Settings"
            onClick={() => setShowSettings(!showSettings)}
          />
        </div>
      </div>

      {/* Stats Bar */}
      <div className="flex items-center gap-4 mb-6 p-4 bg-surface-50 dark:bg-surface-800/50 rounded-xl">
        <div className="flex items-center gap-2">
          <span className="text-sm text-surface-500">Items:</span>
          <Badge variant="default">{batchItems.length}</Badge>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-sm text-surface-500">Pending:</span>
          <Badge variant="warning">{pendingCount}</Badge>
        </div>
        <div className="flex items-center gap-2">
          <span className="text-sm text-surface-500">Completed:</span>
          <Badge variant="success">{completedCount}</Badge>
        </div>
        {errorCount > 0 && (
          <div className="flex items-center gap-2">
            <span className="text-sm text-surface-500">Errors:</span>
            <Badge variant="error">{errorCount}</Badge>
          </div>
        )}

        <div className="flex-1" />

        {/* Model Selector */}
        <select
          value={selectedModel}
          onChange={(e) => setSelectedModel(e.target.value)}
          disabled={modelsLoading || loadedModels.length === 0}
          className="px-3 py-2 rounded-lg bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-brand-500 min-w-[180px]"
        >
          <option value="">Select model...</option>
          {loadedModels.map((model: any) => (
            <option key={model.id} value={model.id}>
              {model.name}
            </option>
          ))}
        </select>

        <Button
          variant="primary"
          onClick={runBatch}
          disabled={batchMutation.isPending || !selectedModel || pendingCount === 0}
        >
          {batchMutation.isPending ? (
            <>
              <Loader2 className="w-4 h-4 animate-spin mr-2" />
              Processing...
            </>
          ) : (
            <>
              <Play className="w-4 h-4" />
              Run Batch
            </>
          )}
        </Button>
      </div>

      <div className="flex gap-6">
        {/* Main Content */}
        <div className="flex-1">
          {/* Preset Buttons */}
          <div className="flex items-center gap-2 mb-4">
            <span className="text-sm text-surface-500">Quick presets:</span>
            {PRESET_PROMPTS.map((preset) => (
              <button
                key={preset.label}
                onClick={() => applyPreset(preset.prompt)}
                className="px-3 py-1.5 text-xs font-medium bg-surface-100 dark:bg-surface-800 hover:bg-surface-200 dark:hover:bg-surface-700 rounded-lg transition-colors"
              >
                {preset.label}
              </button>
            ))}
          </div>

          {/* Batch Items */}
          <div className={clsx(
            'space-y-3',
            viewMode === 'grid' && 'grid grid-cols-2 gap-4 space-y-0'
          )}>
            {batchItems.map((item, index) => (
              <motion.div
                key={item.id}
                initial={{ opacity: 0, y: 10 }}
                animate={{ opacity: 1, y: 0 }}
                className={clsx(
                  'p-4 rounded-xl border transition-colors',
                  item.status === 'completed' && 'border-success-200 dark:border-success-800 bg-success-50/50 dark:bg-success-900/10',
                  item.status === 'error' && 'border-error-200 dark:border-error-800 bg-error-50/50 dark:bg-error-900/10',
                  item.status === 'processing' && 'border-brand-200 dark:border-brand-800 bg-brand-50/50 dark:bg-brand-900/10',
                  item.status === 'pending' && 'border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800'
                )}
              >
                {/* Item Header */}
                <div className="flex items-center justify-between mb-3">
                  <div className="flex items-center gap-2">
                    <span className="text-sm font-medium text-surface-500">#{index + 1}</span>
                    {item.status === 'processing' && (
                      <Badge variant="info">
                        <Loader2 className="w-3 h-3 animate-spin mr-1" />
                        Processing
                      </Badge>
                    )}
                    {item.status === 'completed' && (
                      <Badge variant="success">
                        <CheckCircle2 className="w-3 h-3" />
                        Completed
                      </Badge>
                    )}
                    {item.status === 'error' && (
                      <Badge variant="error">
                        <AlertCircle className="w-3 h-3" />
                        Error
                      </Badge>
                    )}
                    {item.tokensGenerated != null && item.tokensGenerated > 0 && (
                      <span className="text-xs text-surface-500 flex items-center gap-1">
                        <Zap className="w-3 h-3" />
                        {item.tokensGenerated} tok
                      </span>
                    )}
                    {item.generationTime != null && item.generationTime > 0 && (
                      <span className="text-xs text-surface-500 flex items-center gap-1">
                        <Clock className="w-3 h-3" />
                        {item.generationTime.toFixed(0)}ms
                      </span>
                    )}
                  </div>
                  <button
                    onClick={() => removeItem(item.id)}
                    className="p-1.5 text-surface-400 hover:text-error-500 hover:bg-error-50 dark:hover:bg-error-900/20 rounded-lg transition-colors"
                  >
                    <Trash2 className="w-4 h-4" />
                  </button>
                </div>

                {/* Prompt Input */}
                <textarea
                  value={item.prompt}
                  onChange={(e) => updatePrompt(item.id, e.target.value)}
                  placeholder="Enter your prompt..."
                  rows={2}
                  disabled={item.status === 'processing'}
                  className="w-full rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-900 px-3 py-2 text-sm resize-none focus:outline-none focus:ring-2 focus:ring-brand-500 disabled:opacity-50"
                />

                {/* Result */}
                {item.result && (
                  <div className="mt-3 p-3 rounded-lg bg-surface-100 dark:bg-surface-900/50">
                    <div className="flex items-center justify-between mb-2">
                      <span className="text-xs font-medium text-surface-500">Result</span>
                      <button
                        onClick={() => copyResult(item.id, item.result!)}
                        className="p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded transition-colors"
                      >
                        {copiedId === item.id ? (
                          <Check className="w-3.5 h-3.5 text-success-500" />
                        ) : (
                          <Copy className="w-3.5 h-3.5 text-surface-400" />
                        )}
                      </button>
                    </div>
                    <div className="text-sm text-surface-700 dark:text-surface-300">
                      <MarkdownRenderer content={item.result || ''} />
                    </div>
                  </div>
                )}

                {/* Error */}
                {item.error && (
                  <div className="mt-3 p-3 rounded-lg bg-error-50 dark:bg-error-900/20 text-error-700 dark:text-error-300 text-sm">
                    {item.error}
                  </div>
                )}
              </motion.div>
            ))}
          </div>

          {/* Add Button */}
          <button
            onClick={addItem}
            className="w-full mt-4 py-3 border-2 border-dashed border-surface-300 dark:border-surface-700 rounded-xl text-surface-500 hover:border-brand-500 hover:text-brand-500 transition-colors flex items-center justify-center gap-2"
          >
            <Plus className="w-4 h-4" />
            Add Item
          </button>

          {/* Export Button */}
          {completedCount > 0 && (
            <div className="mt-4 flex justify-end">
              <Button variant="secondary" onClick={exportResults}>
                <Download className="w-4 h-4" />
                Export Results
              </Button>
            </div>
          )}
        </div>

        {/* Settings Panel */}
        <AnimatePresence>
          {showSettings && (
            <motion.div
              initial={{ width: 0, opacity: 0 }}
              animate={{ width: 300, opacity: 1 }}
              exit={{ width: 0, opacity: 0 }}
              className="overflow-hidden"
            >
              <Card className="p-4 sticky top-6">
                <h3 className="font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                  <Settings2 className="w-4 h-4" />
                  Batch Settings
                </h3>

                {/* Max Tokens */}
                <div className="mb-4">
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Max Tokens
                    </label>
                    <span className="text-sm text-surface-500">{maxTokens}</span>
                  </div>
                  <input
                    type="range"
                    min="64"
                    max="2048"
                    step="64"
                    value={maxTokens}
                    onChange={(e) => setMaxTokens(parseInt(e.target.value))}
                    className="w-full accent-brand-500"
                  />
                </div>

                {/* Temperature */}
                <div className="mb-4">
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Temperature
                    </label>
                    <span className="text-sm text-surface-500">{temperature.toFixed(2)}</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="2"
                    step="0.05"
                    value={temperature}
                    onChange={(e) => setTemperature(parseFloat(e.target.value))}
                    className="w-full accent-brand-500"
                  />
                </div>

                {/* System Prompt */}
                <div className="mb-4">
                  <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                    System Prompt
                  </label>
                  <textarea
                    value={systemPrompt}
                    onChange={(e) => setSystemPrompt(e.target.value)}
                    placeholder="e.g. You are a classifier. Return valid JSON only."
                    rows={3}
                    className="w-full rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-900 px-3 py-2 text-xs resize-none focus:outline-none focus:ring-2 focus:ring-brand-500"
                  />
                  <p className="text-xs text-surface-500 mt-1">
                    Applied to all items unless overridden per-item
                  </p>
                </div>

                {/* Tool Calling */}
                <div className="pt-4 border-t border-surface-200 dark:border-surface-800">
                  <label className="flex items-center justify-between cursor-pointer mb-4">
                    <div>
                      <span className="text-sm font-medium text-surface-700 dark:text-surface-300 flex items-center gap-2">
                        <Wrench className="w-4 h-4" />
                        Enable Tools
                      </span>
                      <p className="text-xs text-surface-500 mt-0.5">
                        Allow batch to use defined tools
                      </p>
                    </div>
                    <Toggle
                      checked={enableTools}
                      onChange={setEnableTools}
                    />
                  </label>

                  {enableTools && (
                    <div className="space-y-2">
                      <p className="text-xs text-surface-500 mb-2">
                        Defined tools: {tools.length}
                      </p>
                      {tools.map((tool, idx) => (
                        <div
                          key={idx}
                          className="p-2 rounded-lg bg-surface-100 dark:bg-surface-800 text-xs"
                        >
                          <div className="flex items-center gap-2 font-medium text-surface-700 dark:text-surface-300">
                            <Code2 className="w-3 h-3" />
                            {tool.name}
                          </div>
                          <p className="text-surface-500 mt-1">{tool.description}</p>
                        </div>
                      ))}
                    </div>
                  )}
                </div>

                {/* Import/Export */}
                <div className="pt-4 mt-4 border-t border-surface-200 dark:border-surface-800">
                  <h4 className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-3">
                    Import/Export
                  </h4>
                  <div className="flex gap-2">
                    <Button variant="secondary" size="sm" className="flex-1">
                      <Upload className="w-4 h-4" />
                      Import
                    </Button>
                    <Button variant="secondary" size="sm" className="flex-1" onClick={exportResults}>
                      <Download className="w-4 h-4" />
                      Export
                    </Button>
                  </div>
                </div>
              </Card>
            </motion.div>
          )}
        </AnimatePresence>
      </div>
    </div>
  );
}
