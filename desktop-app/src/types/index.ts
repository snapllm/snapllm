// ============================================================================
// SnapLLM Enterprise - Type Definitions
// ============================================================================

// ----------------------------------------------------------------------------
// Core Types
// ----------------------------------------------------------------------------

export type Theme = 'light' | 'dark' | 'system';

export type Status = 'idle' | 'loading' | 'success' | 'error';

export type ModelType = 'llm' | 'diffusion' | 'vision' | 'embedding';

export type ModelStatus = 'idle' | 'loading' | 'ready' | 'generating' | 'error' | 'unloading';

// ----------------------------------------------------------------------------
// User & Authentication
// ----------------------------------------------------------------------------

export interface User {
  id: string;
  email: string;
  name: string;
  avatar?: string;
  role: UserRole;
  organization?: Organization;
  preferences: UserPreferences;
  createdAt: string;
  lastLoginAt: string;
}

export type UserRole = 'admin' | 'developer' | 'user' | 'viewer';

export interface Organization {
  id: string;
  name: string;
  slug: string;
  logo?: string;
  plan: 'free' | 'pro' | 'enterprise';
  settings: OrganizationSettings;
}

export interface OrganizationSettings {
  maxModels: number;
  maxUsers: number;
  maxTokensPerMonth: number;
  allowedFeatures: string[];
}

export interface UserPreferences {
  theme: Theme;
  language: string;
  notifications: NotificationSettings;
  defaultModel?: string;
  sidebarCollapsed: boolean;
}

export interface NotificationSettings {
  email: boolean;
  desktop: boolean;
  modelReady: boolean;
  generationComplete: boolean;
  errors: boolean;
}

// ----------------------------------------------------------------------------
// Models
// ----------------------------------------------------------------------------

export interface Model {
  id: string;
  name: string;
  type: ModelType;
  status: ModelStatus;
  path: string;
  size: number;
  quantization?: string;
  architecture?: string;
  contextLength?: number;
  parameters?: string;
  description?: string;
  tags: string[];
  metadata: ModelMetadata;
  performance: ModelPerformance;
  loadedAt?: string;
  lastUsedAt?: string;
}

export interface ModelMetadata {
  family?: string;
  version?: string;
  license?: string;
  author?: string;
  source?: string;
  trainedOn?: string;
  capabilities?: string[];
}

export interface ModelPerformance {
  tokensPerSecond: number;
  loadTimeMs: number;
  memoryUsageMb: number;
  gpuMemoryMb?: number;
  cacheHitRate?: number;
}

export interface ModelConfig {
  temperature: number;
  topP: number;
  topK: number;
  maxTokens: number;
  repeatPenalty: number;
  presencePenalty: number;
  frequencyPenalty: number;
  seed: number;
  stopSequences: string[];
  systemPrompt: string;
}

export const DEFAULT_MODEL_CONFIG: ModelConfig = {
  temperature: 0.7,
  topP: 0.9,
  topK: 40,
  maxTokens: 2048,
  repeatPenalty: 1.1,
  presencePenalty: 0,
  frequencyPenalty: 0,
  seed: -1,
  stopSequences: [],
  systemPrompt: '',
};

// ----------------------------------------------------------------------------
// Chat & Conversations
// ----------------------------------------------------------------------------

export interface Conversation {
  id: string;
  title: string;
  modelId: string;
  messages: Message[];
  config: ModelConfig;
  createdAt: string;
  updatedAt: string;
  tags: string[];
  starred: boolean;
  archived: boolean;
}

export interface Message {
  id: string;
  role: 'system' | 'user' | 'assistant' | 'tool';
  content: string;
  timestamp: string;
  metadata?: MessageMetadata;
  attachments?: Attachment[];
  toolCalls?: ToolCall[];
  reasoning?: string;
}

export interface MessageMetadata {
  tokensUsed?: number;
  generationTimeMs?: number;
  tokensPerSecond?: number;
  modelId?: string;
  finishReason?: 'stop' | 'length' | 'tool_calls' | 'error';
}

export interface Attachment {
  id: string;
  type: 'image' | 'document' | 'audio';
  name: string;
  url: string;
  size: number;
  mimeType: string;
  thumbnail?: string;
}

export interface ToolCall {
  id: string;
  name: string;
  arguments: Record<string, unknown>;
  result?: unknown;
  status: 'pending' | 'running' | 'completed' | 'error';
}

// ----------------------------------------------------------------------------
// Image Generation
// ----------------------------------------------------------------------------

export interface ImageGenerationParams {
  prompt: string;
  negativePrompt?: string;
  width: number;
  height: number;
  steps: number;
  cfgScale: number;
  seed: number;
  sampler?: string;
  scheduler?: string;
  batchSize: number;
}

export const DEFAULT_IMAGE_PARAMS: ImageGenerationParams = {
  prompt: '',
  negativePrompt: '',
  width: 512,
  height: 512,
  steps: 20,
  cfgScale: 7.0,
  seed: -1,
  sampler: 'euler_a',
  batchSize: 1,
};

export interface GeneratedImage {
  id: string;
  url: string;
  params: ImageGenerationParams;
  modelId: string;
  generationTimeMs: number;
  createdAt: string;
}

// ----------------------------------------------------------------------------
// RAG & Knowledge Base
// ----------------------------------------------------------------------------

export interface KnowledgeBase {
  id: string;
  name: string;
  description?: string;
  documents: Document[];
  embeddingModel: string;
  chunkSize: number;
  chunkOverlap: number;
  totalChunks: number;
  totalTokens: number;
  createdAt: string;
  updatedAt: string;
}

export interface Document {
  id: string;
  name: string;
  type: 'pdf' | 'txt' | 'md' | 'html' | 'docx' | 'csv' | 'json';
  size: number;
  chunks: number;
  status: 'pending' | 'processing' | 'indexed' | 'error';
  uploadedAt: string;
  processedAt?: string;
  error?: string;
}

export interface RAGQuery {
  query: string;
  knowledgeBaseIds: string[];
  topK: number;
  scoreThreshold: number;
  rerank: boolean;
}

export interface RAGResult {
  chunks: RetrievedChunk[];
  query: string;
  totalResults: number;
  searchTimeMs: number;
}

export interface RetrievedChunk {
  id: string;
  documentId: string;
  documentName: string;
  content: string;
  score: number;
  metadata: Record<string, unknown>;
}

// ----------------------------------------------------------------------------
// Agents & Workflows
// ----------------------------------------------------------------------------

export interface Agent {
  id: string;
  name: string;
  description?: string;
  systemPrompt: string;
  modelId: string;
  tools: Tool[];
  knowledgeBases: string[];
  config: AgentConfig;
  createdAt: string;
  updatedAt: string;
}

export interface AgentConfig {
  maxIterations: number;
  maxTokensPerTurn: number;
  temperature: number;
  enableMemory: boolean;
  memoryWindow: number;
}

export interface Tool {
  id: string;
  name: string;
  description: string;
  parameters: ToolParameter[];
  endpoint?: string;
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE';
  headers?: Record<string, string>;
}

export interface ToolParameter {
  name: string;
  type: 'string' | 'number' | 'boolean' | 'array' | 'object';
  description: string;
  required: boolean;
  default?: unknown;
  enum?: string[];
}

export interface Workflow {
  id: string;
  name: string;
  description?: string;
  nodes: WorkflowNode[];
  edges: WorkflowEdge[];
  variables: WorkflowVariable[];
  createdAt: string;
  updatedAt: string;
}

export interface WorkflowNode {
  id: string;
  type: 'start' | 'end' | 'llm' | 'tool' | 'condition' | 'loop' | 'human' | 'rag';
  position: { x: number; y: number };
  data: Record<string, unknown>;
}

export interface WorkflowEdge {
  id: string;
  source: string;
  target: string;
  label?: string;
  condition?: string;
}

export interface WorkflowVariable {
  name: string;
  type: string;
  defaultValue?: unknown;
  description?: string;
}

// ----------------------------------------------------------------------------
// Analytics & Metrics
// ----------------------------------------------------------------------------

export interface Analytics {
  period: 'day' | 'week' | 'month' | 'year';
  totalRequests: number;
  totalTokens: number;
  totalCost: number;
  avgLatency: number;
  avgTokensPerSecond: number;
  modelUsage: ModelUsage[];
  dailyStats: DailyStat[];
}

export interface ModelUsage {
  modelId: string;
  modelName: string;
  requests: number;
  tokens: number;
  cost: number;
  avgLatency: number;
}

export interface DailyStat {
  date: string;
  requests: number;
  tokens: number;
  cost: number;
  errors: number;
}

export interface SystemMetrics {
  cpuUsage: number;
  memoryUsage: number;
  memoryTotal: number;
  gpuUsage?: number;
  gpuMemoryUsage?: number;
  gpuMemoryTotal?: number;
  diskUsage: number;
  diskTotal: number;
  uptime: number;
  activeModels: number;
  queuedRequests: number;
}

// ----------------------------------------------------------------------------
// API Keys & Security
// ----------------------------------------------------------------------------

export interface ApiKey {
  id: string;
  name: string;
  prefix: string;
  createdAt: string;
  lastUsedAt?: string;
  expiresAt?: string;
  permissions: ApiPermission[];
  rateLimit: RateLimit;
  usageStats: ApiKeyUsage;
}

export type ApiPermission =
  | 'models:read'
  | 'models:write'
  | 'chat:read'
  | 'chat:write'
  | 'images:generate'
  | 'rag:read'
  | 'rag:write'
  | 'agents:read'
  | 'agents:write'
  | 'analytics:read';

export interface RateLimit {
  requestsPerMinute: number;
  tokensPerDay: number;
}

export interface ApiKeyUsage {
  totalRequests: number;
  totalTokens: number;
  lastRequest?: string;
}

// ----------------------------------------------------------------------------
// Audit & Logging
// ----------------------------------------------------------------------------

export interface AuditLog {
  id: string;
  userId: string;
  userName: string;
  action: string;
  resource: string;
  resourceId?: string;
  details?: Record<string, unknown>;
  ipAddress: string;
  userAgent: string;
  timestamp: string;
}

// ----------------------------------------------------------------------------
// Notifications
// ----------------------------------------------------------------------------

export interface Notification {
  id: string;
  type: 'info' | 'success' | 'warning' | 'error';
  title: string;
  message: string;
  read: boolean;
  actionUrl?: string;
  actionLabel?: string;
  createdAt: string;
}

// ----------------------------------------------------------------------------
// UI State Types
// ----------------------------------------------------------------------------

export interface ModalState {
  isOpen: boolean;
  type?: string;
  data?: unknown;
}

export interface SidebarState {
  isCollapsed: boolean;
  activeSection?: string;
}

export interface CommandPaletteState {
  isOpen: boolean;
  query: string;
  results: CommandResult[];
}

export interface CommandResult {
  id: string;
  title: string;
  description?: string;
  icon?: string;
  action: () => void;
  keywords?: string[];
}

// ----------------------------------------------------------------------------
// API Response Types
// ----------------------------------------------------------------------------

export interface ApiResponse<T> {
  success: boolean;
  data?: T;
  error?: ApiError;
  meta?: ApiMeta;
}

export interface ApiError {
  code: string;
  message: string;
  details?: Record<string, unknown>;
}

export interface ApiMeta {
  page?: number;
  limit?: number;
  total?: number;
  hasMore?: boolean;
}

export interface PaginatedResponse<T> {
  items: T[];
  page: number;
  limit: number;
  total: number;
  totalPages: number;
}

// ----------------------------------------------------------------------------
// Event Types
// ----------------------------------------------------------------------------

export interface StreamEvent {
  type: 'token' | 'thinking' | 'tool_call' | 'tool_result' | 'done' | 'error';
  data: unknown;
  timestamp: number;
}

export interface GenerationEvent {
  type: 'start' | 'progress' | 'complete' | 'error';
  progress?: number;
  result?: unknown;
  error?: string;
}

// ----------------------------------------------------------------------------
// vPID L2 Context (KV Cache) Types
// ----------------------------------------------------------------------------

export type ContextTier = 'hot' | 'warm' | 'cold';

export interface Context {
  id: string;
  modelId: string;
  tokenCount: number;
  storageSizeMb: number;
  tier: ContextTier;
  createdAt: string;
  lastAccessed: string;
  accessCount: number;
  metadata?: Record<string, unknown>;
}

export interface ContextIngestConfig {
  contextId?: string;
  modelId: string;
  content: string;
  metadata?: Record<string, unknown>;
}

export interface ContextQueryConfig {
  query: string;
  maxTokens?: number;
  temperature?: number;
  topP?: number;
  topK?: number;
  repeatPenalty?: number;
}

export interface ContextQueryResult {
  contextId: string;
  response: string;
  cacheHit: boolean;
  usage: ContextUsage;
  latencyMs: number;
  totalTimeMs: number;
  speedup: string;
}

export interface ContextUsage {
  contextTokens: number;
  queryTokens: number;
  generatedTokens: number;
  totalTokens: number;
}

export interface ContextStats {
  totalContexts: number;
  totalSizeMb: number;
  cacheHits: number;
  cacheMisses: number;
  hitRate: number;
  avgQueryLatencyMs: number;
  tierStats: {
    hot: TierStats;
    warm: TierStats;
    cold: TierStats;
  };
  memoryUsage: {
    gpuMb: number;
    cpuMb: number;
    diskMb: number;
  };
}

export interface TierStats {
  count: number;
  sizeMb: number;
}

export interface ContextListState {
  contexts: Context[];
  count: number;
  totalSizeMb: number;
  tierDistribution: {
    hot: number;
    warm: number;
    cold: number;
  };
}
