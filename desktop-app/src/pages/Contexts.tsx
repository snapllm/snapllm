// ============================================================================
// SnapLLM Enterprise - vPID L2 Context Management
// KV Cache Management for O(1) Context Injection
// ============================================================================

import React, { useState, useCallback, useMemo } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Database,
  Plus,
  Trash2,
  ChevronUp,
  ChevronDown,
  Send,
  Loader2,
  Copy,
  Check,
  RefreshCw,
  Zap,
  Clock,
  HardDrive,
  MemoryStick,
  Cpu,
  Layers,
  TrendingUp,
  TrendingDown,
  ArrowUp,
  ArrowDown,
  FileText,
  MessageSquare,
  Search,
  Filter,
  MoreVertical,
  Info,
  AlertCircle,
  CheckCircle2,
  XCircle,
  Sparkles,
  Timer,
  Hash,
  Target,
} from 'lucide-react';
import { MarkdownRenderer } from '../components/ui/MarkdownRenderer';
import {
  listModels,
  listContexts,
  getContextStats,
  ingestContext,
  queryContext,
  deleteContext,
  promoteContext,
  demoteContext,
  ContextListResponse,
  ContextStatsResponse,
  ContextIngestResponse,
  ContextQueryResponse,
  Context,
} from '../lib/api';
import { Button, IconButton, Badge, Card, Toggle, Modal } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface IngestFormData {
  context_id: string;
  model_id: string;
  content: string;
}

interface QueryFormData {
  query: string;
  max_tokens: number;
  temperature: number;
}

// ============================================================================
// Constants
// ============================================================================

const TIER_COLORS = {
  hot: { bg: 'bg-red-500', text: 'text-red-700 dark:text-red-400', light: 'bg-red-100 dark:bg-red-900/30' },
  warm: { bg: 'bg-yellow-500', text: 'text-yellow-700 dark:text-yellow-400', light: 'bg-yellow-100 dark:bg-yellow-900/30' },
  cold: { bg: 'bg-blue-500', text: 'text-blue-700 dark:text-blue-400', light: 'bg-blue-100 dark:bg-blue-900/30' },
};

const TIER_DESCRIPTIONS = {
  hot: 'GPU VRAM - Instant injection (<0.1ms)',
  warm: 'CPU RAM - Fast restore (<1ms)',
  cold: 'SSD/Disk - Persistent storage (~10ms)',
};

// ============================================================================
// Main Component
// ============================================================================

export default function Contexts() {
  const queryClient = useQueryClient();

  // State
  const [showIngestModal, setShowIngestModal] = useState(false);
  const [selectedContext, setSelectedContext] = useState<Context | null>(null);
  const [showQueryModal, setShowQueryModal] = useState(false);
  const [queryResult, setQueryResult] = useState<ContextQueryResponse | null>(null);
  const [filterTier, setFilterTier] = useState<string>('');
  const [filterModel, setFilterModel] = useState<string>('');
  const [searchQuery, setSearchQuery] = useState('');
  const [copiedId, setCopiedId] = useState<string | null>(null);

  // Ingest form
  const [ingestForm, setIngestForm] = useState<IngestFormData>({
    context_id: '',
    model_id: '',
    content: '',
  });

  // Query form
  const [queryForm, setQueryForm] = useState<QueryFormData>({
    query: '',
    max_tokens: 512,
    temperature: 0.7,
  });

  // API Queries
  const { data: modelsData } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 10000,
  });

  const { data: contextList, isLoading: contextsLoading, refetch: refetchContexts } = useQuery({
    queryKey: ['contexts', filterTier, filterModel],
    queryFn: () => listContexts(filterTier || undefined, filterModel || undefined),
    refetchInterval: 5000,
  });

  const { data: contextStats, refetch: refetchStats } = useQuery({
    queryKey: ['contextStats'],
    queryFn: getContextStats,
    refetchInterval: 5000,
  });

  const loadedModels = modelsData?.models?.filter((m: any) => m.status === 'loaded' || !m.status) || [];

  // Mutations
  const ingestMutation = useMutation({
    mutationFn: ingestContext,
    onSuccess: (data) => {
      queryClient.invalidateQueries({ queryKey: ['contexts'] });
      queryClient.invalidateQueries({ queryKey: ['contextStats'] });
      setShowIngestModal(false);
      setIngestForm({ context_id: '', model_id: '', content: '' });
    },
  });

  const queryMutation = useMutation({
    mutationFn: ({ contextId, request }: { contextId: string; request: any }) =>
      queryContext(contextId, request),
    onSuccess: (data) => {
      setQueryResult(data);
      queryClient.invalidateQueries({ queryKey: ['contextStats'] });
    },
  });

  const deleteMutation = useMutation({
    mutationFn: deleteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['contexts'] });
      queryClient.invalidateQueries({ queryKey: ['contextStats'] });
    },
  });

  const promoteMutation = useMutation({
    mutationFn: promoteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['contexts'] });
      queryClient.invalidateQueries({ queryKey: ['contextStats'] });
    },
  });

  const demoteMutation = useMutation({
    mutationFn: demoteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['contexts'] });
      queryClient.invalidateQueries({ queryKey: ['contextStats'] });
    },
  });

  // Filtered contexts
  const filteredContexts = useMemo(() => {
    if (!contextList?.contexts) return [];
    return contextList.contexts.filter((ctx) => {
      if (searchQuery && !ctx.id.toLowerCase().includes(searchQuery.toLowerCase())) {
        return false;
      }
      return true;
    });
  }, [contextList, searchQuery]);

  // Handle ingest
  const handleIngest = () => {
    if (!ingestForm.model_id || !ingestForm.content.trim()) return;
    ingestMutation.mutate({
      context_id: ingestForm.context_id || undefined,
      model_id: ingestForm.model_id,
      content: ingestForm.content,
    });
  };

  // Handle query
  const handleQuery = () => {
    if (!selectedContext || !queryForm.query.trim()) return;
    queryMutation.mutate({
      contextId: selectedContext.id,
      request: {
        query: queryForm.query,
        max_tokens: queryForm.max_tokens,
        temperature: queryForm.temperature,
      },
    });
  };

  // Copy context ID
  const copyContextId = (id: string) => {
    navigator.clipboard.writeText(id);
    setCopiedId(id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  // Open query modal
  const openQueryModal = (ctx: Context) => {
    setSelectedContext(ctx);
    setQueryResult(null);
    setQueryForm({ query: '', max_tokens: 512, temperature: 0.7 });
    setShowQueryModal(true);
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-purple-500 to-pink-600 flex items-center justify-center">
            <Database className="w-6 h-6 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
              vPID L2 Contexts
            </h1>
            <p className="text-surface-500">
              Pre-computed KV cache for O(1) context injection
            </p>
          </div>
        </div>

        <div className="flex items-center gap-3">
          <Button variant="secondary" onClick={() => { refetchContexts(); refetchStats(); }}>
            <RefreshCw className="w-4 h-4" />
            Refresh
          </Button>
          <Button variant="primary" onClick={() => setShowIngestModal(true)}>
            <Plus className="w-4 h-4" />
            Ingest Context
          </Button>
        </div>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-purple-100 dark:bg-purple-900/30 flex items-center justify-center">
              <Database className="w-5 h-5 text-purple-600 dark:text-purple-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {contextStats?.total_contexts || 0}
              </p>
              <p className="text-sm text-surface-500">Total Contexts</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-green-100 dark:bg-green-900/30 flex items-center justify-center">
              <TrendingUp className="w-5 h-5 text-green-600 dark:text-green-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {contextStats?.hit_rate ? (contextStats.hit_rate * 100).toFixed(0) : 0}%
              </p>
              <p className="text-sm text-surface-500">Cache Hit Rate</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-sky-100 dark:bg-sky-900/30 flex items-center justify-center">
              <Timer className="w-5 h-5 text-sky-600 dark:text-sky-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {contextStats?.avg_query_latency_ms?.toFixed(0) || 0}ms
              </p>
              <p className="text-sm text-surface-500">Avg Query Latency</p>
            </div>
          </div>
        </Card>

        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-orange-100 dark:bg-orange-900/30 flex items-center justify-center">
              <HardDrive className="w-5 h-5 text-orange-600 dark:text-orange-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {contextStats?.total_size_mb?.toFixed(1) || 0} MB
              </p>
              <p className="text-sm text-surface-500">Total Storage</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Tier Distribution */}
      <div className="grid grid-cols-3 gap-4">
        {(['hot', 'warm', 'cold'] as const).map((tier) => {
          const stats = contextStats?.tier_stats?.[tier];
          const colors = TIER_COLORS[tier];
          return (
            <Card key={tier} className={clsx('p-4', colors.light)}>
              <div className="flex items-center gap-2 mb-2">
                <div className={clsx('w-3 h-3 rounded-full', colors.bg)} />
                <span className={clsx('text-sm font-semibold uppercase', colors.text)}>
                  {tier}
                </span>
              </div>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {stats?.count || 0}
                  </p>
                  <p className="text-xs text-surface-500">contexts</p>
                </div>
                <div>
                  <p className="text-2xl font-bold text-surface-900 dark:text-white">
                    {stats?.size_mb?.toFixed(1) || 0}
                  </p>
                  <p className="text-xs text-surface-500">MB</p>
                </div>
              </div>
              <p className="text-xs text-surface-500 mt-2">{TIER_DESCRIPTIONS[tier]}</p>
            </Card>
          );
        })}
      </div>

      {/* Filters */}
      <Card className="p-4">
        <div className="flex items-center gap-4">
          <div className="flex-1">
            <div className="relative">
              <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
              <input
                type="text"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                placeholder="Search contexts by ID..."
                className="w-full pl-10 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
              />
            </div>
          </div>

          <select
            value={filterTier}
            onChange={(e) => setFilterTier(e.target.value)}
            className="px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
          >
            <option value="">All Tiers</option>
            <option value="hot">Hot (GPU)</option>
            <option value="warm">Warm (CPU)</option>
            <option value="cold">Cold (Disk)</option>
          </select>

          <select
            value={filterModel}
            onChange={(e) => setFilterModel(e.target.value)}
            className="px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
          >
            <option value="">All Models</option>
            {loadedModels.map((model: any) => (
              <option key={model.id} value={model.id}>{model.name}</option>
            ))}
          </select>
        </div>
      </Card>

      {/* Context List */}
      <Card className="overflow-hidden">
        <div className="p-4 border-b border-surface-200 dark:border-surface-700">
          <h3 className="text-lg font-semibold text-surface-900 dark:text-white flex items-center gap-2">
            <Layers className="w-5 h-5 text-purple-500" />
            Cached Contexts ({filteredContexts.length})
          </h3>
        </div>

        {contextsLoading ? (
          <div className="flex items-center justify-center py-12">
            <Loader2 className="w-8 h-8 animate-spin text-brand-500" />
          </div>
        ) : filteredContexts.length === 0 ? (
          <div className="text-center py-12">
            <Database className="w-12 h-12 text-surface-300 dark:text-surface-600 mx-auto mb-4" />
            <p className="text-surface-600 dark:text-surface-400 mb-2">No contexts found</p>
            <p className="text-sm text-surface-500 mb-4">
              Ingest documents to pre-compute KV cache for instant context injection
            </p>
            <Button variant="primary" onClick={() => setShowIngestModal(true)}>
              <Plus className="w-4 h-4" />
              Ingest First Context
            </Button>
          </div>
        ) : (
          <div className="divide-y divide-surface-200 dark:divide-surface-700">
            {filteredContexts.map((ctx) => {
              const tierColors = TIER_COLORS[ctx.tier as keyof typeof TIER_COLORS];
              return (
                <motion.div
                  key={ctx.id}
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  className="p-4 hover:bg-surface-50 dark:hover:bg-surface-800/50 transition-colors"
                >
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-4">
                      {/* Tier indicator */}
                      <div className={clsx('w-10 h-10 rounded-lg flex items-center justify-center', tierColors.light)}>
                        <div className={clsx('w-3 h-3 rounded-full', tierColors.bg)} />
                      </div>

                      {/* Context info */}
                      <div>
                        <div className="flex items-center gap-2">
                          <p className="font-medium text-surface-900 dark:text-white">
                            {ctx.id}
                          </p>
                          <button
                            onClick={() => copyContextId(ctx.id)}
                            className="p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded"
                          >
                            {copiedId === ctx.id ? (
                              <Check className="w-3.5 h-3.5 text-green-500" />
                            ) : (
                              <Copy className="w-3.5 h-3.5 text-surface-400" />
                            )}
                          </button>
                        </div>
                        <div className="flex items-center gap-3 text-sm text-surface-500">
                          <span className="flex items-center gap-1">
                            <Cpu className="w-3 h-3" />
                            {ctx.model_id}
                          </span>
                          <span className="flex items-center gap-1">
                            <Hash className="w-3 h-3" />
                            {ctx.token_count} tokens
                          </span>
                          <span className="flex items-center gap-1">
                            <HardDrive className="w-3 h-3" />
                            {ctx.storage_size_mb?.toFixed(1)} MB
                          </span>
                          <span className="flex items-center gap-1">
                            <Target className="w-3 h-3" />
                            {ctx.access_count || 0} accesses
                          </span>
                        </div>
                      </div>
                    </div>

                    {/* Actions */}
                    <div className="flex items-center gap-2">
                      <Badge variant={ctx.tier === 'hot' ? 'error' : ctx.tier === 'warm' ? 'warning' : 'default'}>
                        {ctx.tier.toUpperCase()}
                      </Badge>

                      {ctx.tier !== 'hot' && (
                        <IconButton
                          icon={<ArrowUp className="w-4 h-4" />}
                          label="Promote"
                          onClick={() => promoteMutation.mutate(ctx.id)}
                          disabled={promoteMutation.isPending}
                        />
                      )}

                      {ctx.tier !== 'cold' && (
                        <IconButton
                          icon={<ArrowDown className="w-4 h-4" />}
                          label="Demote"
                          onClick={() => demoteMutation.mutate(ctx.id)}
                          disabled={demoteMutation.isPending}
                        />
                      )}

                      <Button variant="secondary" size="sm" onClick={() => openQueryModal(ctx)}>
                        <MessageSquare className="w-4 h-4" />
                        Query
                      </Button>

                      <IconButton
                        icon={<Trash2 className="w-4 h-4 text-red-500" />}
                        label="Delete"
                        onClick={() => {
                          if (confirm(`Delete context "${ctx.id}"?`)) {
                            deleteMutation.mutate(ctx.id);
                          }
                        }}
                        disabled={deleteMutation.isPending}
                      />
                    </div>
                  </div>
                </motion.div>
              );
            })}
          </div>
        )}
      </Card>

      {/* Ingest Modal */}
      <Modal
        isOpen={showIngestModal}
        onClose={() => setShowIngestModal(false)}
        title="Ingest New Context"
      >
        <div className="space-y-4">
          <div>
            <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
              Context ID (optional)
            </label>
            <input
              type="text"
              value={ingestForm.context_id}
              onChange={(e) => setIngestForm({ ...ingestForm, context_id: e.target.value })}
              placeholder="Auto-generated if empty..."
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
            />
          </div>

          <div>
            <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
              Model *
            </label>
            <select
              value={ingestForm.model_id}
              onChange={(e) => setIngestForm({ ...ingestForm, model_id: e.target.value })}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
            >
              <option value="">Select a model...</option>
              {loadedModels.map((model: any) => (
                <option key={model.id} value={model.id}>{model.name}</option>
              ))}
            </select>
          </div>

          <div>
            <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
              Content *
            </label>
            <textarea
              value={ingestForm.content}
              onChange={(e) => setIngestForm({ ...ingestForm, content: e.target.value })}
              placeholder="Enter the context content to pre-compute KV cache..."
              rows={8}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm resize-none focus:outline-none focus:ring-2 focus:ring-brand-500"
            />
            <p className="text-xs text-surface-500 mt-1">
              ~{Math.ceil(ingestForm.content.length / 4)} tokens (estimated)
            </p>
          </div>

          <div className="flex items-center justify-between pt-4 border-t border-surface-200 dark:border-surface-700">
            <p className="text-xs text-surface-500">
              This will pre-compute the KV cache for O(1) context injection.
              Large contexts may take time to process (O(n^2)).
            </p>
            <div className="flex items-center gap-2">
              <Button variant="ghost" onClick={() => setShowIngestModal(false)}>
                Cancel
              </Button>
              <Button
                variant="primary"
                onClick={handleIngest}
                disabled={!ingestForm.model_id || !ingestForm.content.trim() || ingestMutation.isPending}
              >
                {ingestMutation.isPending ? (
                  <>
                    <Loader2 className="w-4 h-4 animate-spin" />
                    Ingesting...
                  </>
                ) : (
                  <>
                    <Plus className="w-4 h-4" />
                    Ingest Context
                  </>
                )}
              </Button>
            </div>
          </div>

          {ingestMutation.isError && (
            <div className="p-3 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-300 text-sm flex items-start gap-2">
              <AlertCircle className="w-4 h-4 flex-shrink-0 mt-0.5" />
              <span>{(ingestMutation.error as Error)?.message || 'Failed to ingest context'}</span>
            </div>
          )}
        </div>
      </Modal>

      {/* Query Modal */}
      <Modal
        isOpen={showQueryModal}
        onClose={() => setShowQueryModal(false)}
        title={`Query with Context: ${selectedContext?.id}`}
        size="lg"
      >
        <div className="space-y-4">
          {/* Context Info */}
          <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800 flex items-center gap-4">
            <div className={clsx(
              'w-3 h-3 rounded-full',
              TIER_COLORS[selectedContext?.tier as keyof typeof TIER_COLORS]?.bg
            )} />
            <div className="flex-1 text-sm">
              <span className="text-surface-500">Model:</span>{' '}
              <span className="font-medium text-surface-900 dark:text-white">{selectedContext?.model_id}</span>
              <span className="mx-2 text-surface-300">|</span>
              <span className="text-surface-500">Tokens:</span>{' '}
              <span className="font-medium text-surface-900 dark:text-white">{selectedContext?.token_count}</span>
              <span className="mx-2 text-surface-300">|</span>
              <span className="text-surface-500">Tier:</span>{' '}
              <span className="font-medium text-surface-900 dark:text-white uppercase">{selectedContext?.tier}</span>
            </div>
          </div>

          {/* Query Input */}
          <div>
            <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
              Query
            </label>
            <textarea
              value={queryForm.query}
              onChange={(e) => setQueryForm({ ...queryForm, query: e.target.value })}
              placeholder="Enter your query (context is already loaded)..."
              rows={4}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 text-sm resize-none focus:outline-none focus:ring-2 focus:ring-brand-500"
            />
          </div>

          {/* Settings */}
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                Max Tokens: {queryForm.max_tokens}
              </label>
              <input
                type="range"
                min="64"
                max="2048"
                step="64"
                value={queryForm.max_tokens}
                onChange={(e) => setQueryForm({ ...queryForm, max_tokens: parseInt(e.target.value) })}
                className="w-full accent-brand-500"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-surface-700 dark:text-surface-300 mb-1">
                Temperature: {queryForm.temperature.toFixed(1)}
              </label>
              <input
                type="range"
                min="0"
                max="2"
                step="0.1"
                value={queryForm.temperature}
                onChange={(e) => setQueryForm({ ...queryForm, temperature: parseFloat(e.target.value) })}
                className="w-full accent-brand-500"
              />
            </div>
          </div>

          {/* Submit */}
          <div className="flex justify-end gap-2">
            <Button variant="ghost" onClick={() => setShowQueryModal(false)}>
              Cancel
            </Button>
            <Button
              variant="primary"
              onClick={handleQuery}
              disabled={!queryForm.query.trim() || queryMutation.isPending}
            >
              {queryMutation.isPending ? (
                <>
                  <Loader2 className="w-4 h-4 animate-spin" />
                  Querying...
                </>
              ) : (
                <>
                  <Send className="w-4 h-4" />
                  Send Query
                </>
              )}
            </Button>
          </div>

          {/* Query Result */}
          {queryResult && (
            <div className="mt-4 pt-4 border-t border-surface-200 dark:border-surface-700">
              <div className="flex items-center gap-4 mb-3">
                <Badge variant={queryResult.cache_hit ? 'success' : 'default'}>
                  {queryResult.cache_hit ? 'Cache Hit' : 'Cache Miss'}
                </Badge>
                <span className="text-sm text-surface-500">
                  Latency: {queryResult.latency_ms?.toFixed(0) || queryResult.total_time_ms}ms
                </span>
                {queryResult.speedup && (
                  <span className="text-sm text-green-600 dark:text-green-400 font-medium flex items-center gap-1">
                    <Zap className="w-3 h-3" />
                    {queryResult.speedup}
                  </span>
                )}
              </div>

              {/* Token Usage */}
              <div className="flex items-center gap-4 mb-3 text-xs text-surface-500">
                <span>Context: {queryResult.usage?.context_tokens || 0} tokens</span>
                <span>Query: {queryResult.usage?.query_tokens || 0} tokens</span>
                <span>Generated: {queryResult.usage?.generated_tokens || 0} tokens</span>
              </div>

              {/* Response */}
              <div className="p-4 rounded-lg bg-surface-50 dark:bg-surface-800">
                <MarkdownRenderer
                  content={queryResult.response}
                  className="text-sm"
                />
              </div>
            </div>
          )}

          {queryMutation.isError && (
            <div className="p-3 rounded-lg bg-red-50 dark:bg-red-900/20 text-red-700 dark:text-red-300 text-sm flex items-start gap-2">
              <AlertCircle className="w-4 h-4 flex-shrink-0 mt-0.5" />
              <span>{(queryMutation.error as Error)?.message || 'Query failed'}</span>
            </div>
          )}
        </div>
      </Modal>
    </div>
  );
}
