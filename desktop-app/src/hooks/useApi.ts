/**
 * React Query Hooks for SnapLLM API
 *
 * Custom hooks for fetching and mutating data from the FastAPI server
 * Uses TanStack React Query for caching, refetching, and state management
 */

import { useQuery, useMutation, useQueryClient, UseQueryOptions, UseMutationOptions } from '@tanstack/react-query';
import {
  getHealth,
  listModels,
  loadModel,
  switchModel,
  generateText,
  generateBatch,
  getCacheStats,
  handleApiError,
  HealthResponse,
  ModelListResponse,
  ModelLoadRequest,
  ModelLoadResponse,
  ModelSwitchRequest,
  ModelSwitchResponse,
  GenerateRequest,
  GenerateResponse,
  BatchGenerateRequest,
  BatchGenerateResponse,
  CacheStatsResponse,
  // vPID L2 Context APIs
  ingestContext,
  listContexts,
  getContext,
  queryContext,
  deleteContext,
  promoteContext,
  demoteContext,
  getContextStats,
  ContextIngestRequest,
  ContextIngestResponse,
  ContextListResponse,
  Context,
  ContextQueryRequest,
  ContextQueryResponse,
  ContextStatsResponse,
} from '../lib/api';

// ============================================================================
// Query Keys (for cache management)
// ============================================================================

export const queryKeys = {
  health: ['health'] as const,
  models: ['models'] as const,
  modelList: () => [...queryKeys.models, 'list'] as const,
  // vPID L2 Context keys
  contexts: ['contexts'] as const,
  contextList: (tier?: string, modelId?: string) => [...queryKeys.contexts, 'list', tier, modelId] as const,
  context: (id: string) => [...queryKeys.contexts, id] as const,
  contextStats: () => [...queryKeys.contexts, 'stats'] as const,
};

// ============================================================================
// Health Check Hook
// ============================================================================

export const useHealth = (options?: UseQueryOptions<HealthResponse, Error>) => {
  return useQuery<HealthResponse, Error>({
    queryKey: queryKeys.health,
    queryFn: getHealth,
    refetchInterval: 10000, // Refetch every 10 seconds
    retry: 1,
    ...options,
  });
};

// ============================================================================
// Model Management Hooks
// ============================================================================

export const useModels = (options?: UseQueryOptions<ModelListResponse, Error>) => {
  return useQuery<ModelListResponse, Error>({
    queryKey: queryKeys.modelList(),
    queryFn: listModels,
    refetchInterval: 5000, // Refetch every 5 seconds
    ...options,
  });
};

export const useLoadModel = (
  options?: UseMutationOptions<ModelLoadResponse, Error, ModelLoadRequest>
) => {
  const queryClient = useQueryClient();

  return useMutation<ModelLoadResponse, Error, ModelLoadRequest>({
    mutationFn: loadModel,
    onSuccess: () => {
      // Invalidate models list to trigger refetch
      queryClient.invalidateQueries({ queryKey: queryKeys.modelList() });
      queryClient.invalidateQueries({ queryKey: queryKeys.health });
    },
    onError: (error) => {
      console.error('[useLoadModel] Error:', handleApiError(error));
    },
    ...options,
  });
};

export const useSwitchModel = (
  options?: UseMutationOptions<ModelSwitchResponse, Error, ModelSwitchRequest>
) => {
  const queryClient = useQueryClient();

  return useMutation<ModelSwitchResponse, Error, ModelSwitchRequest>({
    mutationFn: switchModel,
    onSuccess: () => {
      // Invalidate models list to update active model
      queryClient.invalidateQueries({ queryKey: queryKeys.modelList() });
    },
    onError: (error) => {
      console.error('[useSwitchModel] Error:', handleApiError(error));
    },
    ...options,
  });
};

// ============================================================================
// Text Generation Hooks
// ============================================================================

export const useGenerateText = (
  options?: UseMutationOptions<GenerateResponse, Error, GenerateRequest>
) => {
  return useMutation<GenerateResponse, Error, GenerateRequest>({
    mutationFn: generateText,
    onError: (error) => {
      console.error('[useGenerateText] Error:', handleApiError(error));
    },
    ...options,
  });
};

export const useGenerateBatch = (
  options?: UseMutationOptions<BatchGenerateResponse, Error, BatchGenerateRequest>
) => {
  return useMutation<BatchGenerateResponse, Error, BatchGenerateRequest>({
    mutationFn: generateBatch,
    onError: (error) => {
      console.error('[useGenerateBatch] Error:', handleApiError(error));
    },
    ...options,
  });
};

// ============================================================================
// Utility Hooks
// ============================================================================

/**
 * Hook to check if API server is connected
 */
export const useServerStatus = () => {
  const { data: health, isError, isLoading } = useHealth();

  return {
    isConnected: !isError && health?.status === 'ok',
    isChecking: isLoading,
    serverInfo: health,
  };
};

/**
 * Hook to get the current active model
 */
export const useCurrentModel = () => {
  const { data: modelsData } = useModels();

  return {
    currentModel: modelsData?.current_model || null,
    models: modelsData?.models || [],
    totalModels: modelsData?.count || 0,
  };
};

/**
 * Hook for error notification with user-friendly messages
 */
export const useApiErrorHandler = () => {
  const formatError = (error: unknown): string => {
    return handleApiError(error);
  };

  return { formatError };
};

/**
 * Hook to get cache statistics for vPID system
 */
export const useCacheStats = () => {
  return useQuery<CacheStatsResponse, Error>({
    queryKey: ['cacheStats'],
    queryFn: getCacheStats,
    refetchInterval: 5000,
    retry: 1,
  });
};

// ============================================================================
// vPID L2 Context (KV Cache) Hooks
// ============================================================================

/**
 * Hook to list all contexts
 */
export const useContextList = (tier?: string, modelId?: string, options?: UseQueryOptions<ContextListResponse, Error>) => {
  return useQuery<ContextListResponse, Error>({
    queryKey: queryKeys.contextList(tier, modelId),
    queryFn: () => listContexts(tier, modelId),
    refetchInterval: 10000, // Refetch every 10 seconds
    ...options,
  });
};

/**
 * Hook to get a specific context
 */
export const useContext = (contextId: string, options?: UseQueryOptions<Context, Error>) => {
  return useQuery<Context, Error>({
    queryKey: queryKeys.context(contextId),
    queryFn: () => getContext(contextId),
    enabled: !!contextId,
    ...options,
  });
};

/**
 * Hook to get context statistics (vPID L2 metrics)
 */
export const useContextStats = (options?: UseQueryOptions<ContextStatsResponse, Error>) => {
  return useQuery<ContextStatsResponse, Error>({
    queryKey: queryKeys.contextStats(),
    queryFn: getContextStats,
    refetchInterval: 5000, // Refetch every 5 seconds
    retry: 1,
    ...options,
  });
};

/**
 * Hook to ingest a new context (pre-compute KV cache)
 */
export const useIngestContext = (
  options?: UseMutationOptions<ContextIngestResponse, Error, ContextIngestRequest>
) => {
  const queryClient = useQueryClient();

  return useMutation<ContextIngestResponse, Error, ContextIngestRequest>({
    mutationFn: ingestContext,
    onSuccess: () => {
      // Invalidate context list and stats to trigger refetch
      queryClient.invalidateQueries({ queryKey: queryKeys.contexts });
    },
    onError: (error) => {
      console.error('[useIngestContext] Error:', handleApiError(error));
    },
    ...options,
  });
};

/**
 * Hook to query with cached context
 */
export const useQueryContext = (
  options?: UseMutationOptions<ContextQueryResponse, Error, { contextId: string; request: ContextQueryRequest }>
) => {
  const queryClient = useQueryClient();

  return useMutation<ContextQueryResponse, Error, { contextId: string; request: ContextQueryRequest }>({
    mutationFn: ({ contextId, request }) => queryContext(contextId, request),
    onSuccess: () => {
      // Update stats after query
      queryClient.invalidateQueries({ queryKey: queryKeys.contextStats() });
    },
    onError: (error) => {
      console.error('[useQueryContext] Error:', handleApiError(error));
    },
    ...options,
  });
};

/**
 * Hook to delete a context
 */
export const useDeleteContext = (
  options?: UseMutationOptions<{ status: string; message: string }, Error, string>
) => {
  const queryClient = useQueryClient();

  return useMutation<{ status: string; message: string }, Error, string>({
    mutationFn: deleteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.contexts });
    },
    onError: (error) => {
      console.error('[useDeleteContext] Error:', handleApiError(error));
    },
    ...options,
  });
};

/**
 * Hook to promote context to hot tier (GPU)
 */
export const usePromoteContext = (
  options?: UseMutationOptions<{ status: string; message: string; new_tier: string }, Error, string>
) => {
  const queryClient = useQueryClient();

  return useMutation<{ status: string; message: string; new_tier: string }, Error, string>({
    mutationFn: promoteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.contexts });
    },
    onError: (error) => {
      console.error('[usePromoteContext] Error:', handleApiError(error));
    },
    ...options,
  });
};

/**
 * Hook to demote context to cold tier (disk)
 */
export const useDemoteContext = (
  options?: UseMutationOptions<{ status: string; message: string; new_tier: string }, Error, string>
) => {
  const queryClient = useQueryClient();

  return useMutation<{ status: string; message: string; new_tier: string }, Error, string>({
    mutationFn: demoteContext,
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.contexts });
    },
    onError: (error) => {
      console.error('[useDemoteContext] Error:', handleApiError(error));
    },
    ...options,
  });
};
