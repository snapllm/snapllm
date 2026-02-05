// ============================================================================
// SnapLLM Enterprise - Global State Store (Zustand)
// ============================================================================

import { create } from 'zustand';
import { persist, devtools } from 'zustand/middleware';
import { immer } from 'zustand/middleware/immer';
import type {
  Theme,
  User,
  Model,
  ModelConfig,
  Conversation,
  Message,
  KnowledgeBase,
  Agent,
  Workflow,
  Notification,
  ApiKey,
  SystemMetrics,
  DEFAULT_MODEL_CONFIG,
} from '../types';

// ----------------------------------------------------------------------------
// App Store - Global UI State
// ----------------------------------------------------------------------------

interface AppState {
  // Theme
  theme: Theme;
  setTheme: (theme: Theme) => void;

  // Sidebar
  sidebarCollapsed: boolean;
  toggleSidebar: () => void;
  setSidebarCollapsed: (collapsed: boolean) => void;

  // Command Palette
  commandPaletteOpen: boolean;
  setCommandPaletteOpen: (open: boolean) => void;

  // Notifications
  notifications: Notification[];
  addNotification: (notification: Omit<Notification, 'id' | 'createdAt' | 'read'>) => void;
  markNotificationRead: (id: string) => void;
  clearNotifications: () => void;

  // Modals
  activeModal: { type: string; data?: unknown } | null;
  openModal: (type: string, data?: unknown) => void;
  closeModal: () => void;

  // Global Loading
  globalLoading: boolean;
  setGlobalLoading: (loading: boolean) => void;
}

export const useAppStore = create<AppState>()(
  devtools(
    persist(
      immer((set) => ({
        // Theme
        theme: 'system',
        setTheme: (theme) => set((state) => { state.theme = theme; }),

        // Sidebar
        sidebarCollapsed: false,
        toggleSidebar: () => set((state) => { state.sidebarCollapsed = !state.sidebarCollapsed; }),
        setSidebarCollapsed: (collapsed) => set((state) => { state.sidebarCollapsed = collapsed; }),

        // Command Palette
        commandPaletteOpen: false,
        setCommandPaletteOpen: (open) => set((state) => { state.commandPaletteOpen = open; }),

        // Notifications
        notifications: [],
        addNotification: (notification) => set((state) => {
          state.notifications.unshift({
            ...notification,
            id: crypto.randomUUID(),
            createdAt: new Date().toISOString(),
            read: false,
          });
          // Keep only last 50 notifications
          if (state.notifications.length > 50) {
            state.notifications = state.notifications.slice(0, 50);
          }
        }),
        markNotificationRead: (id) => set((state) => {
          const notification = state.notifications.find((n) => n.id === id);
          if (notification) notification.read = true;
        }),
        clearNotifications: () => set((state) => { state.notifications = []; }),

        // Modals
        activeModal: null,
        openModal: (type, data) => set((state) => { state.activeModal = { type, data }; }),
        closeModal: () => set((state) => { state.activeModal = null; }),

        // Global Loading
        globalLoading: false,
        setGlobalLoading: (loading) => set((state) => { state.globalLoading = loading; }),
      })),
      {
        name: 'snapllm-app-store',
        partialize: (state) => ({
          theme: state.theme,
          sidebarCollapsed: state.sidebarCollapsed,
        }),
      }
    ),
    { name: 'AppStore' }
  )
);

// ----------------------------------------------------------------------------
// Auth Store - User Authentication State
// ----------------------------------------------------------------------------

interface AuthState {
  user: User | null;
  isAuthenticated: boolean;
  isLoading: boolean;
  error: string | null;

  setUser: (user: User | null) => void;
  login: (email: string, password: string) => Promise<void>;
  logout: () => void;
  updatePreferences: (preferences: Partial<User['preferences']>) => void;
}

export const useAuthStore = create<AuthState>()(
  devtools(
    persist(
      immer((set, get) => ({
        user: null,
        isAuthenticated: false,
        isLoading: false,
        error: null,

        setUser: (user) => set((state) => {
          state.user = user;
          state.isAuthenticated = !!user;
        }),

        login: async (email, password) => {
          set((state) => { state.isLoading = true; state.error = null; });
          try {
            // Mock login - replace with actual API call
            const mockUser: User = {
              id: '1',
              email,
              name: email.split('@')[0],
              role: 'admin',
              preferences: {
                theme: 'system',
                language: 'en',
                notifications: {
                  email: true,
                  desktop: true,
                  modelReady: true,
                  generationComplete: true,
                  errors: true,
                },
                sidebarCollapsed: false,
              },
              createdAt: new Date().toISOString(),
              lastLoginAt: new Date().toISOString(),
            };
            set((state) => {
              state.user = mockUser;
              state.isAuthenticated = true;
              state.isLoading = false;
            });
          } catch (error) {
            set((state) => {
              state.error = 'Login failed';
              state.isLoading = false;
            });
          }
        },

        logout: () => set((state) => {
          state.user = null;
          state.isAuthenticated = false;
        }),

        updatePreferences: (preferences) => set((state) => {
          if (state.user) {
            state.user.preferences = { ...state.user.preferences, ...preferences };
          }
        }),
      })),
      {
        name: 'snapllm-auth-store',
        partialize: (state) => ({
          user: state.user,
          isAuthenticated: state.isAuthenticated,
        }),
      }
    ),
    { name: 'AuthStore' }
  )
);

// ----------------------------------------------------------------------------
// Model Store - Model Management State
// ----------------------------------------------------------------------------

interface ModelState {
  models: Model[];
  activeModelId: string | null;
  loadingModels: Set<string>;
  modelConfigs: Record<string, ModelConfig>;

  setModels: (models: Model[]) => void;
  addModel: (model: Model) => void;
  updateModel: (id: string, updates: Partial<Model>) => void;
  removeModel: (id: string) => void;
  setActiveModel: (id: string | null) => void;
  setModelLoading: (id: string, loading: boolean) => void;
  setModelConfig: (id: string, config: Partial<ModelConfig>) => void;
  getActiveModel: () => Model | undefined;
}

export const useModelStore = create<ModelState>()(
  devtools(
    immer((set, get) => ({
      models: [],
      activeModelId: null,
      loadingModels: new Set(),
      modelConfigs: {},

      setModels: (models) => set((state) => { state.models = models; }),

      addModel: (model) => set((state) => {
        const exists = state.models.find((m) => m.id === model.id);
        if (!exists) state.models.push(model);
      }),

      updateModel: (id, updates) => set((state) => {
        const index = state.models.findIndex((m) => m.id === id);
        if (index !== -1) {
          state.models[index] = { ...state.models[index], ...updates };
        }
      }),

      removeModel: (id) => set((state) => {
        state.models = state.models.filter((m) => m.id !== id);
        if (state.activeModelId === id) state.activeModelId = null;
      }),

      setActiveModel: (id) => set((state) => { state.activeModelId = id; }),

      setModelLoading: (id, loading) => set((state) => {
        if (loading) {
          state.loadingModels.add(id);
        } else {
          state.loadingModels.delete(id);
        }
      }),

      setModelConfig: (id, config) => set((state) => {
        state.modelConfigs[id] = {
          ...state.modelConfigs[id],
          ...config,
        };
      }),

      getActiveModel: () => {
        const state = get();
        return state.models.find((m) => m.id === state.activeModelId);
      },
    })),
    { name: 'ModelStore' }
  )
);

// ----------------------------------------------------------------------------
// Chat Store - Conversation State
// ----------------------------------------------------------------------------

interface ChatState {
  conversations: Conversation[];
  activeConversationId: string | null;
  isGenerating: boolean;
  streamingMessage: string;

  setConversations: (conversations: Conversation[]) => void;
  addConversation: (conversation: Conversation) => void;
  updateConversation: (id: string, updates: Partial<Conversation>) => void;
  deleteConversation: (id: string) => void;
  setActiveConversation: (id: string | null) => void;

  addMessage: (conversationId: string, message: Message) => void;
  updateMessage: (conversationId: string, messageId: string, updates: Partial<Message>) => void;
  deleteMessage: (conversationId: string, messageId: string) => void;
  clearMessages: (conversationId: string) => void;

  setIsGenerating: (generating: boolean) => void;
  setStreamingMessage: (message: string) => void;
  appendStreamingMessage: (chunk: string) => void;

  getActiveConversation: () => Conversation | undefined;
  createNewConversation: (modelId: string) => string;
}

export const useChatStore = create<ChatState>()(
  devtools(
    persist(
      immer((set, get) => ({
        conversations: [],
        activeConversationId: null,
        isGenerating: false,
        streamingMessage: '',

        setConversations: (conversations) => set((state) => { state.conversations = conversations; }),

        addConversation: (conversation) => set((state) => {
          state.conversations.unshift(conversation);
        }),

        updateConversation: (id, updates) => set((state) => {
          const index = state.conversations.findIndex((c) => c.id === id);
          if (index !== -1) {
            state.conversations[index] = { ...state.conversations[index], ...updates };
          }
        }),

        deleteConversation: (id) => set((state) => {
          state.conversations = state.conversations.filter((c) => c.id !== id);
          if (state.activeConversationId === id) state.activeConversationId = null;
        }),

        setActiveConversation: (id) => set((state) => { state.activeConversationId = id; }),

        addMessage: (conversationId, message) => set((state) => {
          const conversation = state.conversations.find((c) => c.id === conversationId);
          if (conversation) {
            conversation.messages.push(message);
            conversation.updatedAt = new Date().toISOString();
          }
        }),

        updateMessage: (conversationId, messageId, updates) => set((state) => {
          const conversation = state.conversations.find((c) => c.id === conversationId);
          if (conversation) {
            const message = conversation.messages.find((m) => m.id === messageId);
            if (message) {
              Object.assign(message, updates);
            }
          }
        }),

        deleteMessage: (conversationId, messageId) => set((state) => {
          const conversation = state.conversations.find((c) => c.id === conversationId);
          if (conversation) {
            conversation.messages = conversation.messages.filter((m) => m.id !== messageId);
          }
        }),

        clearMessages: (conversationId) => set((state) => {
          const conversation = state.conversations.find((c) => c.id === conversationId);
          if (conversation) {
            conversation.messages = [];
          }
        }),

        setIsGenerating: (generating) => set((state) => { state.isGenerating = generating; }),
        setStreamingMessage: (message) => set((state) => { state.streamingMessage = message; }),
        appendStreamingMessage: (chunk) => set((state) => { state.streamingMessage += chunk; }),

        getActiveConversation: () => {
          const state = get();
          return state.conversations.find((c) => c.id === state.activeConversationId);
        },

        createNewConversation: (modelId) => {
          const id = crypto.randomUUID();
          const conversation: Conversation = {
            id,
            title: 'New Conversation',
            modelId,
            messages: [],
            config: {
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
            },
            createdAt: new Date().toISOString(),
            updatedAt: new Date().toISOString(),
            tags: [],
            starred: false,
            archived: false,
          };
          set((state) => {
            state.conversations.unshift(conversation);
            state.activeConversationId = id;
          });
          return id;
        },
      })),
      {
        name: 'snapllm-chat-store',
        partialize: (state) => ({
          conversations: state.conversations.slice(0, 100), // Keep last 100 conversations
        }),
      }
    ),
    { name: 'ChatStore' }
  )
);

// ----------------------------------------------------------------------------
// RAG Store - Knowledge Base State
// ----------------------------------------------------------------------------

interface RAGState {
  knowledgeBases: KnowledgeBase[];
  activeKnowledgeBaseId: string | null;
  isIndexing: boolean;
  indexingProgress: number;

  setKnowledgeBases: (kbs: KnowledgeBase[]) => void;
  addKnowledgeBase: (kb: KnowledgeBase) => void;
  updateKnowledgeBase: (id: string, updates: Partial<KnowledgeBase>) => void;
  deleteKnowledgeBase: (id: string) => void;
  setActiveKnowledgeBase: (id: string | null) => void;
  setIsIndexing: (indexing: boolean) => void;
  setIndexingProgress: (progress: number) => void;
}

export const useRAGStore = create<RAGState>()(
  devtools(
    immer((set) => ({
      knowledgeBases: [],
      activeKnowledgeBaseId: null,
      isIndexing: false,
      indexingProgress: 0,

      setKnowledgeBases: (kbs) => set((state) => { state.knowledgeBases = kbs; }),

      addKnowledgeBase: (kb) => set((state) => { state.knowledgeBases.push(kb); }),

      updateKnowledgeBase: (id, updates) => set((state) => {
        const index = state.knowledgeBases.findIndex((kb) => kb.id === id);
        if (index !== -1) {
          state.knowledgeBases[index] = { ...state.knowledgeBases[index], ...updates };
        }
      }),

      deleteKnowledgeBase: (id) => set((state) => {
        state.knowledgeBases = state.knowledgeBases.filter((kb) => kb.id !== id);
        if (state.activeKnowledgeBaseId === id) state.activeKnowledgeBaseId = null;
      }),

      setActiveKnowledgeBase: (id) => set((state) => { state.activeKnowledgeBaseId = id; }),
      setIsIndexing: (indexing) => set((state) => { state.isIndexing = indexing; }),
      setIndexingProgress: (progress) => set((state) => { state.indexingProgress = progress; }),
    })),
    { name: 'RAGStore' }
  )
);

// ----------------------------------------------------------------------------
// Agent Store - Agent & Workflow State
// ----------------------------------------------------------------------------

interface AgentState {
  agents: Agent[];
  workflows: Workflow[];
  activeAgentId: string | null;
  activeWorkflowId: string | null;

  setAgents: (agents: Agent[]) => void;
  addAgent: (agent: Agent) => void;
  updateAgent: (id: string, updates: Partial<Agent>) => void;
  deleteAgent: (id: string) => void;
  setActiveAgent: (id: string | null) => void;

  setWorkflows: (workflows: Workflow[]) => void;
  addWorkflow: (workflow: Workflow) => void;
  updateWorkflow: (id: string, updates: Partial<Workflow>) => void;
  deleteWorkflow: (id: string) => void;
  setActiveWorkflow: (id: string | null) => void;
}

export const useAgentStore = create<AgentState>()(
  devtools(
    immer((set) => ({
      agents: [],
      workflows: [],
      activeAgentId: null,
      activeWorkflowId: null,

      setAgents: (agents) => set((state) => { state.agents = agents; }),
      addAgent: (agent) => set((state) => { state.agents.push(agent); }),
      updateAgent: (id, updates) => set((state) => {
        const index = state.agents.findIndex((a) => a.id === id);
        if (index !== -1) {
          state.agents[index] = { ...state.agents[index], ...updates };
        }
      }),
      deleteAgent: (id) => set((state) => {
        state.agents = state.agents.filter((a) => a.id !== id);
        if (state.activeAgentId === id) state.activeAgentId = null;
      }),
      setActiveAgent: (id) => set((state) => { state.activeAgentId = id; }),

      setWorkflows: (workflows) => set((state) => { state.workflows = workflows; }),
      addWorkflow: (workflow) => set((state) => { state.workflows.push(workflow); }),
      updateWorkflow: (id, updates) => set((state) => {
        const index = state.workflows.findIndex((w) => w.id === id);
        if (index !== -1) {
          state.workflows[index] = { ...state.workflows[index], ...updates };
        }
      }),
      deleteWorkflow: (id) => set((state) => {
        state.workflows = state.workflows.filter((w) => w.id !== id);
        if (state.activeWorkflowId === id) state.activeWorkflowId = null;
      }),
      setActiveWorkflow: (id) => set((state) => { state.activeWorkflowId = id; }),
    })),
    { name: 'AgentStore' }
  )
);

// ----------------------------------------------------------------------------
// Metrics Store - System Metrics State
// ----------------------------------------------------------------------------

interface MetricsState {
  systemMetrics: SystemMetrics | null;
  isConnected: boolean;
  lastUpdated: string | null;

  setSystemMetrics: (metrics: SystemMetrics) => void;
  setIsConnected: (connected: boolean) => void;
}

export const useMetricsStore = create<MetricsState>()(
  devtools(
    immer((set) => ({
      systemMetrics: null,
      isConnected: false,
      lastUpdated: null,

      setSystemMetrics: (metrics) => set((state) => {
        state.systemMetrics = metrics;
        state.lastUpdated = new Date().toISOString();
      }),
      setIsConnected: (connected) => set((state) => { state.isConnected = connected; }),
    })),
    { name: 'MetricsStore' }
  )
);
