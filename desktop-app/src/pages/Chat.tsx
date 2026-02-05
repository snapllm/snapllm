// ============================================================================
// SnapLLM Enterprise - Chat Interface
// Premium AI Chat Experience with Real-time Streaming
// ============================================================================

import React, { useState, useEffect, useRef, useMemo, useCallback } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Send,
  Bot,
  User,
  Settings2,
  Sparkles,
  Copy,
  Check,
  RotateCcw,
  Trash2,
  Download,
  Upload,
  Plus,
  ChevronDown,
  ChevronRight,
  Sliders,
  Zap,
  Clock,
  MessageSquare,
  Cpu,
  Brain,
  Wand2,
  Target,
  Shuffle,
  History,
  Star,
  StarOff,
  MoreVertical,
  Edit3,
  Share2,
  Bookmark,
  ThumbsUp,
  ThumbsDown,
  RefreshCw,
  PanelRightOpen,
  PanelRightClose,
  Image as ImageIcon,
  StopCircle,
  PlayCircle,
  Code2,
  FileText,
  Hash,
  AlertCircle,
  Eye,
  EyeOff,
  Lightbulb,
  ChevronUp,
  Layers,
  Radio,
} from 'lucide-react';
import { listLLMModels, sendChatMessage, StreamingClient, StreamToken } from '../lib/api';
import { filterChainOfThought, extractChainOfThought, hasChainOfThought } from '../utils/chainOfThought';
import { useModelStore, useChatStore, useAppStore } from '../store';
import { Button, IconButton, Badge, Avatar, Card, Tooltip, Toggle, Slider, Textarea } from '../components/ui';
import { MarkdownRenderer } from '../components/ui/MarkdownRenderer';

// ============================================================================
// Types
// ============================================================================

interface Message {
  id: string;
  role: 'user' | 'assistant' | 'system';
  content: string;
  timestamp: Date;
  tokensPerSecond?: number;
  totalTokens?: number;
  latencyMs?: number;
  modelId?: string;
  attachments?: Attachment[];
  feedback?: 'positive' | 'negative' | null;
  isStreaming?: boolean;
  thinking?: string | null;  // Extended thinking content
  reasoning?: string[];      // Chain-of-thought reasoning steps
  // vPID L2 Context metrics
  cacheHit?: boolean;
  contextTokens?: number;
  speedup?: string;
}

interface Attachment {
  id: string;
  type: 'image' | 'file' | 'code';
  name: string;
  url?: string;
  content?: string;
}

interface Conversation {
  id: string;
  title: string;
  messages: Message[];
  modelId: string;
  createdAt: Date;
  updatedAt: Date;
  starred: boolean;
}

interface GenerationSettings {
  max_tokens: number;
  temperature: number;
  top_p: number;
  top_k: number;
  repeat_penalty: number;
  presence_penalty: number;
  frequency_penalty: number;
  seed: number;
  stop_sequences: string[];
  system_prompt: string;
  filter_chain_of_thought: boolean;
  enable_extended_thinking: boolean;
  thinking_budget_tokens: number;
  use_streaming: boolean;
  use_context_cache: boolean; // vPID L2: Enable KV cache for O(1) context lookups
}

// ============================================================================
// Presets
// ============================================================================

const PRESETS = {
  balanced: {
    name: 'Balanced',
    icon: Target,
    description: 'Good balance of creativity and coherence',
    settings: { temperature: 0.7, top_p: 0.95, top_k: 40, repeat_penalty: 1.1 },
  },
  creative: {
    name: 'Creative',
    icon: Sparkles,
    description: 'More varied and creative responses',
    settings: { temperature: 1.0, top_p: 0.95, top_k: 50, repeat_penalty: 1.05 },
  },
  precise: {
    name: 'Precise',
    icon: Target,
    description: 'Focused and deterministic output',
    settings: { temperature: 0.3, top_p: 0.9, top_k: 20, repeat_penalty: 1.15 },
  },
  code: {
    name: 'Code',
    icon: Code2,
    description: 'Optimized for code generation',
    settings: { temperature: 0.2, top_p: 0.95, top_k: 30, repeat_penalty: 1.0 },
  },
};

const DEFAULT_STOP_SEQUENCES = [
  '**END OF RESPONSE**', '</think>', '<end_of_turn>', '<|eot_id|>', '<|end|>', '</s>',
];

// ============================================================================
// Main Component
// ============================================================================

export default function Chat() {
  const { models, activeModelId, setActiveModel } = useModelStore();
  const { theme } = useAppStore();

  // Local state
  const [conversations, setConversations] = useState<Conversation[]>([]);
  const [activeConversationId, setActiveConversationId] = useState<string | null>(null);
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState('');
  const [selectedModel, setSelectedModel] = useState(activeModelId || '');
  const [showSettings, setShowSettings] = useState(false);
  const [showHistory, setShowHistory] = useState(true);
  const [copiedMessageId, setCopiedMessageId] = useState<string | null>(null);
  const [editingMessageId, setEditingMessageId] = useState<string | null>(null);
  const [activePreset, setActivePreset] = useState<string>('balanced');

  const messagesEndRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLTextAreaElement>(null);

  // Generation settings - streaming disabled by default (server uses SSE, not WebSocket)
  const [settings, setSettings] = useState<GenerationSettings>({
    max_tokens: 2048,
    temperature: 0.7,
    top_p: 0.95,
    top_k: 40,
    repeat_penalty: 1.1,
    presence_penalty: 0.0,
    frequency_penalty: 0.0,
    seed: -1,
    stop_sequences: DEFAULT_STOP_SEQUENCES,
    system_prompt: 'You are a helpful, harmless, and honest AI assistant.',
    filter_chain_of_thought: true,
    enable_extended_thinking: false,
    thinking_budget_tokens: 1024,
    use_streaming: true,  // SSE streaming enabled - shows live tok/s
    use_context_cache: true, // vPID L2 enabled - O(1) context lookups
  });

  // Streaming state
  const [streamingClient] = useState(() => new StreamingClient());
  const [streamingText, setStreamingText] = useState('');
  const [isStreaming, setIsStreaming] = useState(false);
  const [streamingMetrics, setStreamingMetrics] = useState({
    startTime: 0,
    tokenCount: 0,
    tokensPerSecond: 0,
  });

  // Refs to maintain current values in async callbacks (prevents stale closure issues)
  const streamingTextRef = useRef('');
  const streamingMetricsRef = useRef({ startTime: 0, tokenCount: 0 });
  const onCompleteCalledRef = useRef(false);

  // Expanded thinking state (which message's thinking is expanded)
  const [expandedThinking, setExpandedThinking] = useState<Set<string>>(new Set());

  // LocalStorage keys
  const STORAGE_KEYS = {
    messages: 'snapllm_chat_messages',
    conversations: 'snapllm_conversations',
    activeConversation: 'snapllm_active_conversation',
  };

  // Load persisted data on mount
  useEffect(() => {
    try {
      const savedMessages = localStorage.getItem(STORAGE_KEYS.messages);
      const savedConversations = localStorage.getItem(STORAGE_KEYS.conversations);
      const savedActiveConv = localStorage.getItem(STORAGE_KEYS.activeConversation);

      if (savedMessages) {
        const parsed = JSON.parse(savedMessages);
        // Restore Date objects
        const restored = parsed.map((m: any) => ({
          ...m,
          timestamp: new Date(m.timestamp),
        }));
        setMessages(restored);
      }

      if (savedConversations) {
        const parsed = JSON.parse(savedConversations);
        const restored = parsed.map((c: any) => ({
          ...c,
          createdAt: new Date(c.createdAt),
          updatedAt: new Date(c.updatedAt),
          messages: c.messages.map((m: any) => ({
            ...m,
            timestamp: new Date(m.timestamp),
          })),
        }));
        setConversations(restored);
      }

      if (savedActiveConv) {
        setActiveConversationId(savedActiveConv);
      }
    } catch (error) {
      console.error('[Chat] Error loading persisted data:', error);
    }
  }, []);

  // Persist messages when they change
  useEffect(() => {
    if (messages.length > 0) {
      try {
        localStorage.setItem(STORAGE_KEYS.messages, JSON.stringify(messages));
      } catch (error) {
        console.error('[Chat] Error persisting messages:', error);
      }
    }
  }, [messages]);

  // Sync messages to active conversation
  useEffect(() => {
    if (activeConversationId && messages.length > 0) {
      setConversations(prev => prev.map(conv =>
        conv.id === activeConversationId
          ? { ...conv, messages, updatedAt: new Date() }
          : conv
      ));
    }
  }, [messages, activeConversationId]);

  // Persist conversations when they change
  useEffect(() => {
    if (conversations.length > 0) {
      try {
        localStorage.setItem(STORAGE_KEYS.conversations, JSON.stringify(conversations));
      } catch (error) {
        console.error('[Chat] Error persisting conversations:', error);
      }
    }
  }, [conversations]);

  // Persist active conversation ID
  useEffect(() => {
    try {
      if (activeConversationId) {
        localStorage.setItem(STORAGE_KEYS.activeConversation, activeConversationId);
      }
    } catch (error) {
      console.error('[Chat] Error persisting active conversation:', error);
    }
  }, [activeConversationId]);

  // Load messages when conversation is selected from sidebar
  useEffect(() => {
    if (activeConversationId) {
      const conv = conversations.find(c => c.id === activeConversationId);
      if (conv && conv.messages) {
        setMessages(conv.messages);
        if (conv.modelId) {
          setSelectedModel(conv.modelId);
        }
      }
    }
  }, [activeConversationId]); // Only run when activeConversationId changes, not conversations

  // Fetch LLM models only (filtered for chat)
  const { data: modelsResponse, isLoading: modelsLoading } = useQuery({
    queryKey: ['models', 'llm'],
    queryFn: listLLMModels,
    refetchInterval: 5000,
  });

  const loadedModels = modelsResponse?.models?.filter((m: any) => m.status === 'loaded') || [];

  // Chat mutation
  const chatMutation = useMutation({
    mutationFn: sendChatMessage,
    onSuccess: (data) => {
      let rawContent = data.response || data.choices?.[0]?.message?.content || '';

      // Extract thinking/reasoning from the response
      const extracted = extractChainOfThought(rawContent);
      let content = settings.filter_chain_of_thought ? extracted.cleanResponse : rawContent;

      const assistantMessage: Message = {
        id: crypto.randomUUID(),
        role: 'assistant',
        content,
        timestamp: new Date(),
        tokensPerSecond: data.usage?.tokens_per_second,
        totalTokens: data.usage?.total_tokens,
        latencyMs: data.usage?.latency_ms,
        modelId: selectedModel,
        thinking: extracted.thinking,
        reasoning: extracted.reasoning,
        // vPID L2 Context metrics
        cacheHit: data.cache_hit,
        contextTokens: data.usage?.context_tokens,
        speedup: data.speedup,
      };

      setMessages(prev => [...prev, assistantMessage]);
    },
    onError: (error: any) => {
      const errorMessage: Message = {
        id: crypto.randomUUID(),
        role: 'assistant',
        content: `Error: ${error.response?.data?.error?.message || error.message || 'Unknown error'}`,
        timestamp: new Date(),
      };
      setMessages(prev => [...prev, errorMessage]);
    },
  });

  // Toggle thinking expansion for a message
  const toggleThinking = (messageId: string) => {
    setExpandedThinking(prev => {
      const newSet = new Set(prev);
      if (newSet.has(messageId)) {
        newSet.delete(messageId);
      } else {
        newSet.add(messageId);
      }
      return newSet;
    });
  };

  // Stop streaming
  const stopStreaming = useCallback(() => {
    streamingClient.close();
    setIsStreaming(false);

    // Mark as complete to prevent onComplete from also saving
    onCompleteCalledRef.current = true;

    // Use refs for reliable access to current values
    const currentText = streamingTextRef.current;
    const startTime = streamingMetricsRef.current.startTime;
    const tokenCount = streamingMetricsRef.current.tokenCount;

    if (currentText) {
      // Calculate final metrics
      const finalElapsed = startTime > 0 ? Date.now() - startTime : 0;
      const finalTps = finalElapsed > 0 ? (tokenCount / (finalElapsed / 1000)) : 0;

      const extracted = extractChainOfThought(currentText);
      const content = settings.filter_chain_of_thought ? extracted.cleanResponse : currentText;
      const assistantMessage: Message = {
        id: crypto.randomUUID(),
        role: 'assistant',
        content,
        timestamp: new Date(),
        modelId: selectedModel,
        thinking: extracted.thinking,
        reasoning: extracted.reasoning,
        // Include metrics even when stopped early
        tokensPerSecond: finalTps,
        totalTokens: tokenCount,
        latencyMs: finalElapsed,
      };
      setMessages(prev => [...prev, assistantMessage]);
    }

    // Clear all streaming state
    setStreamingText('');
    streamingTextRef.current = '';
    streamingMetricsRef.current = { startTime: 0, tokenCount: 0 };
    setStreamingMetrics({ startTime: 0, tokenCount: 0, tokensPerSecond: 0 });
  }, [streamingClient, settings.filter_chain_of_thought, selectedModel]);

  // Auto-scroll to bottom
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  // Sync selected model with store or auto-select first available model
  useEffect(() => {
    if (activeModelId && !selectedModel) {
      setSelectedModel(activeModelId);
    } else if (!selectedModel && loadedModels.length > 0) {
      // Auto-select first loaded model if none selected
      const firstModel = loadedModels[0];
      setSelectedModel(firstModel.id);
      setActiveModel(firstModel.id);
    }
  }, [activeModelId, selectedModel, loadedModels, setActiveModel]);

  // Handle send
  const handleSend = useCallback(() => {
    if (!input.trim() || !selectedModel) return;

    const userMessage: Message = {
      id: crypto.randomUUID(),
      role: 'user',
      content: input,
      timestamp: new Date(),
    };

    // Create a new conversation if one doesn't exist
    let currentConvId = activeConversationId;
    if (!currentConvId) {
      const newConvId = crypto.randomUUID();
      const title = input.length > 50 ? input.slice(0, 50) + '...' : input;
      const newConv: Conversation = {
        id: newConvId,
        title,
        messages: [userMessage],
        modelId: selectedModel,
        createdAt: new Date(),
        updatedAt: new Date(),
        starred: false,
      };
      setConversations(prev => [newConv, ...prev]);
      setActiveConversationId(newConvId);
      currentConvId = newConvId;
    } else {
      // Update existing conversation with new message
      setConversations(prev => prev.map(conv =>
        conv.id === currentConvId
          ? { ...conv, messages: [...conv.messages, userMessage], updatedAt: new Date() }
          : conv
      ));
    }

    setMessages(prev => [...prev, userMessage]);
    setInput('');

    // Limit context to prevent OOM - keep last N messages and truncate long content
    const MAX_CONTEXT_MESSAGES = 10; // Keep last 10 messages (5 turns)
    const MAX_MESSAGE_LENGTH = 1500; // Truncate messages longer than 1500 chars

    const recentMessages = [...messages, userMessage].slice(-MAX_CONTEXT_MESSAGES);
    const chatMessages = recentMessages.map(m => ({
      role: m.role,
      content: m.content.length > MAX_MESSAGE_LENGTH
        ? m.content.slice(0, MAX_MESSAGE_LENGTH) + '...'
        : m.content,
    }));

    // Build ISON tabular context for older messages (proper ISON v1.0 format)
    // Format: table.context with turn/role/content fields
    const buildISONContext = (msgs: typeof chatMessages) => {
      if (msgs.length <= 2) return null; // No context needed for first turn

      const contextMsgs = msgs.slice(0, -1); // All except current message

      // ISON string serializer (follows ISON v1.0 spec)
      const serializeString = (s: string): string => {
        // Compress whitespace: newlines and multiple spaces become single space
        s = s.replace(/\s+/g, ' ').trim();

        // Check if quoting is needed per ISON spec
        const needsQuotes =
          s.includes(' ') ||
          s.includes('\t') ||
          s.includes('"') ||
          s.includes('\\') ||
          s === 'true' ||
          s === 'false' ||
          s === 'null' ||
          s.startsWith(':') ||
          /^-?\d+(\.\d+)?$/.test(s);

        if (!needsQuotes) return s;

        // Escape and quote
        const escaped = s
          .replace(/\\/g, '\\\\')
          .replace(/"/g, '\\"')
          .replace(/\n/g, '\\n')
          .replace(/\t/g, '\\t');

        return `"${escaped}"`;
      };

      let turnNum = 1;
      const rows: string[] = [
        '# Conversation history',
        'table.context',
        'turn role content'
      ];

      for (let i = 0; i < contextMsgs.length; i++) {
        const m = contextMsgs[i];
        const role = m.role === 'user' ? 'U' : 'A';
        const content = serializeString(m.content);
        rows.push(`${turnNum} ${role} ${content}`);
        if (m.role === 'assistant') turnNum++;
      }

      return rows.join('\n');
    };

    // Use streaming if enabled
    if (settings.use_streaming) {
      setIsStreaming(true);
      setStreamingText('');

      // Initialize streaming metrics and refs for reliable access in callbacks
      const streamStartTime = Date.now();
      streamingTextRef.current = '';
      streamingMetricsRef.current = { startTime: streamStartTime, tokenCount: 0 };
      onCompleteCalledRef.current = false;  // Reset guard for new stream
      setStreamingMetrics({ startTime: streamStartTime, tokenCount: 0, tokensPerSecond: 0 });

      // Capture model at start (won't change during stream)
      const streamModelId = selectedModel;
      const filterCoT = settings.filter_chain_of_thought;

      // Build prompt based on model type - each model family has its own chat template
      const modelLower = selectedModel.toLowerCase();
      const isonContext = buildISONContext(chatMessages);
      const currentMessage = chatMessages[chatMessages.length - 1];
      const userMsg = currentMessage.content;
      const sysPrompt = isonContext || settings.system_prompt;

      // Detect model family from name
      const isGemma = modelLower.includes('gemma');
      const isLlama3 = modelLower.includes('llama') && (modelLower.includes('3') || modelLower.includes('3.'));
      const isLlama2 = modelLower.includes('llama') && !isLlama3;
      const isMistral = modelLower.includes('mistral') || modelLower.includes('mixtral');
      const isQwen = modelLower.includes('qwen');
      const isPhi = modelLower.includes('phi');
      const isDeepSeek = modelLower.includes('deepseek');
      const isTinyLlama = modelLower.includes('tinyllama');
      const isOpenChat = modelLower.includes('openchat');
      const isVicuna = modelLower.includes('vicuna');
      const isZephyr = modelLower.includes('zephyr');
      const isOrcaMini = modelLower.includes('orca');
      const isChatML = isQwen || isDeepSeek || modelLower.includes('yi-') || modelLower.includes('internlm');
      const isGPTOSS = modelLower.includes('gpt-oss') || modelLower.includes('gptoss');

      let prompt: string;

      if (isGemma) {
        // Gemma format
        prompt = sysPrompt
          ? `<start_of_turn>system\n${sysPrompt}<end_of_turn>\n<start_of_turn>user\n${userMsg}<end_of_turn>\n<start_of_turn>model\n`
          : `<start_of_turn>user\n${userMsg}<end_of_turn>\n<start_of_turn>model\n`;
      } else if (isLlama3) {
        // Llama 3 format
        prompt = sysPrompt
          ? `<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\n${sysPrompt}<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n${userMsg}<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n`
          : `<|begin_of_text|><|start_header_id|>user<|end_header_id|>\n\n${userMsg}<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n`;
      } else if (isMistral) {
        // Mistral/Mixtral format
        prompt = sysPrompt
          ? `<s>[INST] ${sysPrompt}\n\n${userMsg} [/INST]`
          : `<s>[INST] ${userMsg} [/INST]`;
      } else if (isChatML || isGPTOSS) {
        // ChatML format (Qwen, DeepSeek, Yi, InternLM, GPT-OSS)
        prompt = sysPrompt
          ? `<|im_start|>system\n${sysPrompt}<|im_end|>\n<|im_start|>user\n${userMsg}<|im_end|>\n<|im_start|>assistant\n`
          : `<|im_start|>user\n${userMsg}<|im_end|>\n<|im_start|>assistant\n`;
      } else if (isPhi) {
        // Phi format
        prompt = sysPrompt
          ? `<|system|>\n${sysPrompt}<|end|>\n<|user|>\n${userMsg}<|end|>\n<|assistant|>\n`
          : `<|user|>\n${userMsg}<|end|>\n<|assistant|>\n`;
      } else if (isTinyLlama || isZephyr) {
        // TinyLlama/Zephyr format
        prompt = sysPrompt
          ? `<|system|>\n${sysPrompt}</s>\n<|user|>\n${userMsg}</s>\n<|assistant|>\n`
          : `<|user|>\n${userMsg}</s>\n<|assistant|>\n`;
      } else if (isOpenChat) {
        // OpenChat format
        prompt = sysPrompt
          ? `GPT4 Correct System: ${sysPrompt}<|end_of_turn|>GPT4 Correct User: ${userMsg}<|end_of_turn|>GPT4 Correct Assistant:`
          : `GPT4 Correct User: ${userMsg}<|end_of_turn|>GPT4 Correct Assistant:`;
      } else if (isVicuna || isLlama2) {
        // Vicuna/Llama2 format
        prompt = sysPrompt
          ? `${sysPrompt}\n\nUSER: ${userMsg}\nASSISTANT:`
          : `USER: ${userMsg}\nASSISTANT:`;
      } else if (isOrcaMini) {
        // Orca Mini format
        prompt = sysPrompt
          ? `### System:\n${sysPrompt}\n\n### User:\n${userMsg}\n\n### Assistant:\n`
          : `### User:\n${userMsg}\n\n### Assistant:\n`;
      } else {
        // Generic/Alpaca format (fallback)
        prompt = sysPrompt
          ? `### System:\n${sysPrompt}\n\n### Human:\n${userMsg}\n\n### Assistant:\n`
          : `### Human:\n${userMsg}\n\n### Assistant:\n`;
      }

      console.log('[Chat] Using template for model:', modelLower.substring(0, 20), '→',
        isGemma ? 'Gemma' : isLlama3 ? 'Llama3' : isMistral ? 'Mistral' :
        isChatML ? 'ChatML' : isPhi ? 'Phi' : isTinyLlama ? 'TinyLlama' :
        isOpenChat ? 'OpenChat' : isVicuna ? 'Vicuna' : isOrcaMini ? 'Orca' : 'Generic');

      // Build messages array for context caching (server needs this for vPID L2)
      const streamMessages = chatMessages.map(m => ({
        role: m.role as 'user' | 'assistant' | 'system',
        content: m.content,
      }));

      // Add system prompt if set
      if (settings.system_prompt) {
        streamMessages.unshift({ role: 'system' as const, content: settings.system_prompt });
      }

      console.log('[Chat] Sending', streamMessages.length, 'messages for context caching');

      streamingClient.connect(
        {
          messages: streamMessages,  // Send full history for vPID L2 context caching
          max_tokens: settings.max_tokens,
          model: selectedModel,
          use_context_cache: settings.use_context_cache,
        },
        (token: StreamToken) => {
          // Update text (both state and ref for reliable access)
          setStreamingText(prev => {
            const newText = prev + token.token;
            streamingTextRef.current = newText;  // Keep ref in sync
            return newText;
          });

          // Update metrics (both state and ref)
          streamingMetricsRef.current.tokenCount++;
          const elapsed = (Date.now() - streamStartTime) / 1000;
          const tps = elapsed > 0 ? streamingMetricsRef.current.tokenCount / elapsed : 0;
          setStreamingMetrics({
            startTime: streamStartTime,
            tokenCount: streamingMetricsRef.current.tokenCount,
            tokensPerSecond: tps,
          });
        },
        (error: Error) => {
          console.error('[Chat] Streaming error:', error);
          setIsStreaming(false);
          streamingTextRef.current = '';
          streamingMetricsRef.current = { startTime: 0, tokenCount: 0 };
          setStreamingMetrics({ startTime: 0, tokenCount: 0, tokensPerSecond: 0 });
          const errorMessage: Message = {
            id: crypto.randomUUID(),
            role: 'assistant',
            content: `Streaming error: ${error.message}`,
            timestamp: new Date(),
          };
          setMessages(prev => [...prev, errorMessage]);
        },
        () => {
          // Guard against double onComplete calls
          if (onCompleteCalledRef.current) {
            console.warn('[Chat] onComplete called multiple times - ignoring duplicate');
            return;
          }
          onCompleteCalledRef.current = true;

          // Get current values from refs (not stale closures)
          const currentText = streamingTextRef.current;
          const tokenCount = streamingMetricsRef.current.tokenCount;
          const finalElapsed = Date.now() - streamStartTime;
          const finalTps = finalElapsed > 0 ? (tokenCount / (finalElapsed / 1000)) : 0;

          console.log('[Chat] Streaming complete:', {
            textLength: currentText.length,
            tokenCount,
            elapsedMs: finalElapsed,
            tps: finalTps.toFixed(1),
          });

          setIsStreaming(false);

          // Save message using ref value (guaranteed to be current)
          if (currentText) {
            try {
              const extracted = extractChainOfThought(currentText);
              const content = filterCoT ? extracted.cleanResponse : currentText;
              const assistantMessage: Message = {
                id: crypto.randomUUID(),
                role: 'assistant',
                content,
                timestamp: new Date(),
                modelId: streamModelId,
                thinking: extracted.thinking,
                reasoning: extracted.reasoning,
                tokensPerSecond: finalTps,
                totalTokens: tokenCount,
                latencyMs: finalElapsed,
              };
              setMessages(prev => [...prev, assistantMessage]);
              console.log('[Chat] Message saved successfully');
            } catch (error) {
              console.error('[Chat] Error saving message:', error);
              // Still show the response even if processing failed
              const fallbackMessage: Message = {
                id: crypto.randomUUID(),
                role: 'assistant',
                content: currentText,
                timestamp: new Date(),
                modelId: streamModelId,
                tokensPerSecond: finalTps,
                totalTokens: tokenCount,
                latencyMs: finalElapsed,
              };
              setMessages(prev => [...prev, fallbackMessage]);
            }
          } else {
            console.warn('[Chat] onComplete called with empty text');
          }

          // Clear streaming state
          setStreamingText('');
          streamingTextRef.current = '';
          streamingMetricsRef.current = { startTime: 0, tokenCount: 0 };
          setStreamingMetrics({ startTime: 0, tokenCount: 0, tokensPerSecond: 0 });
        }
      );
    } else {
      // Use regular mutation with ISON context
      const isonContext = buildISONContext(chatMessages);
      const currentMessage = chatMessages[chatMessages.length - 1];

      // Build messages array with ISON context as system message
      const apiMessages = isonContext
        ? [
            { role: 'system', content: `${isonContext}\n\nRespond to the user's latest message based on the conversation history above.` },
            currentMessage,
          ]
        : chatMessages;

      chatMutation.mutate({
        model: selectedModel,
        messages: apiMessages,
        max_tokens: settings.max_tokens,
        temperature: settings.temperature,
        top_p: settings.top_p,
        top_k: settings.top_k,
        repeat_penalty: settings.repeat_penalty,
        presence_penalty: settings.presence_penalty,
        frequency_penalty: settings.frequency_penalty,
        seed: settings.seed === -1 ? undefined : settings.seed,
        stop: settings.stop_sequences.length > 0 ? settings.stop_sequences : undefined,
        // Extended thinking configuration (Anthropic-style)
        ...(settings.enable_extended_thinking && {
          thinking: {
            type: 'enabled',
            budget_tokens: settings.thinking_budget_tokens,
          },
        }),
      });
    }
  }, [input, selectedModel, messages, settings, chatMutation, streamingClient]);

  // Handle key press
  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  // Copy message
  const copyMessage = (message: Message) => {
    navigator.clipboard.writeText(message.content);
    setCopiedMessageId(message.id);
    setTimeout(() => setCopiedMessageId(null), 2000);
  };

  // Load preset
  const loadPreset = (presetKey: string) => {
    const preset = PRESETS[presetKey as keyof typeof PRESETS];
    if (preset) {
      setSettings(prev => ({ ...prev, ...preset.settings }));
      setActivePreset(presetKey);
    }
  };

  // New conversation / Clear chat
  const newConversation = () => {
    setMessages([]);
    setActiveConversationId(null);
    // Clear persisted messages
    try {
      localStorage.removeItem(STORAGE_KEYS.messages);
      localStorage.removeItem(STORAGE_KEYS.activeConversation);
    } catch (error) {
      console.error('[Chat] Error clearing persisted data:', error);
    }
    inputRef.current?.focus();
  };

  // Handle feedback (thumbs up/down)
  const handleFeedback = (messageId: string, feedback: 'positive' | 'negative') => {
    setMessages(prev => prev.map(msg =>
      msg.id === messageId
        ? { ...msg, feedback: msg.feedback === feedback ? null : feedback }
        : msg
    ));
  };

  // Regenerate response
  const handleRegenerate = (messageId: string) => {
    const messageIndex = messages.findIndex(m => m.id === messageId);
    if (messageIndex === -1) return;

    // Find the user message that triggered this response
    const userMessageIndex = messages.slice(0, messageIndex).findLastIndex(m => m.role === 'user');
    if (userMessageIndex === -1) return;

    const userMessage = messages[userMessageIndex];

    // Remove the assistant message we're regenerating
    setMessages(prev => prev.filter(m => m.id !== messageId));

    // Re-send the user message
    setInput(userMessage.content);
    setTimeout(() => handleSend(), 100);
  };


  // Get active model info
  const activeModel = loadedModels.find((m: any) => m.id === selectedModel);

  return (
    <div className="flex h-[calc(100vh-4rem)] overflow-hidden -m-6">
      {/* Conversation History Sidebar */}
      <AnimatePresence>
        {showHistory && (
          <motion.div
            initial={{ width: 0, opacity: 0 }}
            animate={{ width: 280, opacity: 1 }}
            exit={{ width: 0, opacity: 0 }}
            className="border-r border-surface-200 dark:border-surface-800 bg-surface-50 dark:bg-surface-900 overflow-hidden"
          >
            <div className="h-full flex flex-col">
              {/* Header */}
              <div className="p-4 border-b border-surface-200 dark:border-surface-800">
                <Button
                  variant="primary"
                  className="w-full"
                  onClick={newConversation}
                >
                  <Plus className="w-4 h-4" />
                  New Chat
                </Button>
              </div>

              {/* Conversations List */}
              <div className="flex-1 overflow-y-auto p-2 space-y-1">
                {conversations.length === 0 ? (
                  <div className="text-center py-8 text-surface-500">
                    <MessageSquare className="w-8 h-8 mx-auto mb-2 opacity-50" />
                    <p className="text-sm">No conversations yet</p>
                  </div>
                ) : (
                  conversations.map(conv => (
                    <button
                      key={conv.id}
                      onClick={() => setActiveConversationId(conv.id)}
                      className={clsx(
                        'w-full flex items-center gap-3 px-3 py-2.5 rounded-lg text-left transition-colors',
                        activeConversationId === conv.id
                          ? 'bg-brand-50 dark:bg-brand-900/20 text-brand-700 dark:text-brand-300'
                          : 'hover:bg-surface-100 dark:hover:bg-surface-800'
                      )}
                    >
                      <MessageSquare className="w-4 h-4 flex-shrink-0" />
                      <div className="flex-1 min-w-0">
                        <p className="text-sm font-medium truncate">{conv.title}</p>
                        <p className="text-xs text-surface-500 truncate">
                          {conv.messages.length} messages
                        </p>
                      </div>
                      {conv.starred && <Star className="w-3.5 h-3.5 text-warning-500 fill-warning-500" />}
                    </button>
                  ))
                )}
              </div>
            </div>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Main Chat Area */}
      <div className="flex-1 flex flex-col min-w-0">
        {/* Chat Header */}
        <div className="flex items-center justify-between px-6 py-3 border-b border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900">
          <div className="flex items-center gap-4">
            <IconButton
              icon={showHistory ? <PanelRightClose /> : <PanelRightOpen />}
              label="Toggle history"
              onClick={() => setShowHistory(!showHistory)}
            />

            {/* Model Selector */}
            <div className="relative">
              <select
                value={selectedModel}
                onChange={(e) => {
                  setSelectedModel(e.target.value);
                  setActiveModel(e.target.value);
                }}
                disabled={modelsLoading || loadedModels.length === 0}
                className="appearance-none pl-10 pr-8 py-2 rounded-xl bg-surface-100 dark:bg-surface-800 border border-surface-200 dark:border-surface-700 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-brand-500 min-w-[200px]"
              >
                <option value="">Select model...</option>
                {loadedModels.map((model: any) => (
                  <option key={model.id} value={model.id}>
                    {model.name}
                  </option>
                ))}
              </select>
              <Cpu className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
              <ChevronDown className="absolute right-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
            </div>

            {activeModel && (
              <div className="flex items-center gap-2">
                <Badge variant="success" dot>Ready</Badge>
                <span className="text-xs text-surface-500">
                  {activeModel.performance?.tokensPerSecond?.toFixed(0) || '—'} tok/s
                </span>
              </div>
            )}
          </div>

          <div className="flex items-center gap-2">
            {messages.length > 0 && (
              <Button variant="ghost" size="sm" onClick={newConversation}>
                <Trash2 className="w-4 h-4 mr-1.5" />
                Clear
              </Button>
            )}
            <IconButton
              icon={<Settings2 className={clsx(showSettings && 'text-brand-500')} />}
              label="Settings"
              onClick={() => setShowSettings(!showSettings)}
            />
          </div>
        </div>

        {/* Messages Container */}
        <div className="flex-1 overflow-y-auto">
          {messages.length === 0 ? (
            // Welcome Screen
            <div className="h-full flex flex-col items-center justify-center p-8">
              <div className="w-20 h-20 rounded-2xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center mb-6">
                <Sparkles className="w-10 h-10 text-white" />
              </div>
              <h2 className="text-2xl font-bold text-surface-900 dark:text-white mb-2">
                Start a Conversation
              </h2>
              <p className="text-surface-500 text-center max-w-md mb-8">
                Select a model and send a message to begin. Your conversation will appear here.
              </p>

              {/* Quick Prompts */}
              <div className="grid grid-cols-2 gap-3 max-w-xl w-full">
                {[
                  { icon: Code2, text: 'Help me write code', prompt: 'Help me write a function that...' },
                  { icon: FileText, text: 'Explain a concept', prompt: 'Explain the concept of...' },
                  { icon: Brain, text: 'Brainstorm ideas', prompt: 'Help me brainstorm ideas for...' },
                  { icon: Wand2, text: 'Creative writing', prompt: 'Write a creative story about...' },
                ].map((item, i) => (
                  <button
                    key={i}
                    onClick={() => setInput(item.prompt)}
                    className="flex items-center gap-3 p-4 rounded-xl border border-surface-200 dark:border-surface-700 hover:border-brand-500 dark:hover:border-brand-500 hover:bg-brand-50 dark:hover:bg-brand-900/10 transition-all text-left group"
                  >
                    <div className="w-10 h-10 rounded-lg bg-surface-100 dark:bg-surface-800 group-hover:bg-brand-100 dark:group-hover:bg-brand-900/30 flex items-center justify-center transition-colors">
                      <item.icon className="w-5 h-5 text-surface-500 group-hover:text-brand-600" />
                    </div>
                    <span className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      {item.text}
                    </span>
                  </button>
                ))}
              </div>
            </div>
          ) : (
            // Messages List
            <div className="max-w-4xl mx-auto py-6 px-4 space-y-6">
              {messages.map((message) => (
                <motion.div
                  key={message.id}
                  initial={{ opacity: 0, y: 10 }}
                  animate={{ opacity: 1, y: 0 }}
                  className={clsx(
                    'group flex gap-4',
                    message.role === 'user' ? 'flex-row-reverse' : ''
                  )}
                >
                  {/* Avatar */}
                  <div className={clsx(
                    'w-9 h-9 rounded-xl flex items-center justify-center flex-shrink-0',
                    message.role === 'user'
                      ? 'bg-gradient-to-br from-surface-700 to-surface-900'
                      : 'bg-gradient-to-br from-brand-500 to-ai-purple'
                  )}>
                    {message.role === 'user'
                      ? <User className="w-5 h-5 text-white" />
                      : <Bot className="w-5 h-5 text-white" />
                    }
                  </div>

                  {/* Message Content */}
                  <div className={clsx(
                    'flex-1 max-w-[85%]',
                    message.role === 'user' ? 'text-right' : ''
                  )}>
                    {/* Extended Thinking Section (for assistant messages with thinking) */}
                    {message.role === 'assistant' && message.thinking && (
                      <div className="mb-2">
                        <button
                          onClick={() => toggleThinking(message.id)}
                          className="flex items-center gap-2 px-3 py-1.5 rounded-lg bg-amber-50 dark:bg-amber-900/20 text-amber-700 dark:text-amber-300 text-sm font-medium hover:bg-amber-100 dark:hover:bg-amber-900/30 transition-colors"
                        >
                          <Lightbulb className="w-4 h-4" />
                          <span>Extended Thinking</span>
                          {expandedThinking.has(message.id) ? (
                            <ChevronUp className="w-4 h-4" />
                          ) : (
                            <ChevronDown className="w-4 h-4" />
                          )}
                        </button>
                        <AnimatePresence>
                          {expandedThinking.has(message.id) && (
                            <motion.div
                              initial={{ height: 0, opacity: 0 }}
                              animate={{ height: 'auto', opacity: 1 }}
                              exit={{ height: 0, opacity: 0 }}
                              className="overflow-hidden"
                            >
                              <div className="mt-2 p-3 rounded-lg bg-amber-50 dark:bg-amber-900/10 border border-amber-200 dark:border-amber-800 text-sm text-surface-700 dark:text-surface-300">
                                <div className="flex items-center gap-2 mb-2 text-amber-600 dark:text-amber-400 font-medium">
                                  <Brain className="w-4 h-4" />
                                  <span>Model's Reasoning Process</span>
                                </div>
                                <pre className="whitespace-pre-wrap font-mono text-xs leading-relaxed">
                                  {message.thinking}
                                </pre>
                              </div>
                            </motion.div>
                          )}
                        </AnimatePresence>
                      </div>
                    )}

                    <div className={clsx(
                      'inline-block rounded-2xl px-4 py-3 max-w-full overflow-hidden',
                      message.role === 'user'
                        ? 'bg-brand-600 text-white rounded-tr-md'
                        : 'bg-surface-100 dark:bg-surface-800 text-surface-900 dark:text-surface-100 rounded-tl-md'
                    )}>
                      {message.role === 'user' ? (
                        <p className="whitespace-pre-wrap text-[15px] leading-relaxed">
                          {message.content}
                        </p>
                      ) : (
                        <MarkdownRenderer
                          content={message.content}
                          className="text-[15px]"
                        />
                      )}
                    </div>

                    {/* Message Meta */}
                    <div className={clsx(
                      'flex items-center gap-3 mt-1.5 text-xs text-surface-500',
                      message.role === 'user' ? 'justify-end' : ''
                    )}>
                      <span>{message.timestamp.toLocaleTimeString()}</span>
                      {message.role === 'assistant' && message.tokensPerSecond && (
                        <span className="flex items-center gap-1">
                          <Zap className="w-3 h-3" />
                          {message.tokensPerSecond.toFixed(1)} tok/s
                        </span>
                      )}
                      {message.role === 'assistant' && message.latencyMs && (
                        <span className="flex items-center gap-1">
                          <Clock className="w-3 h-3" />
                          {message.latencyMs}ms
                        </span>
                      )}
                      {/* vPID L2 Cache Hit Indicator */}
                      {message.role === 'assistant' && message.cacheHit !== undefined && (
                        <span className={clsx(
                          'flex items-center gap-1 px-1.5 py-0.5 rounded-full text-[10px] font-medium',
                          message.cacheHit
                            ? 'bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400'
                            : 'bg-gray-100 dark:bg-gray-800 text-gray-600 dark:text-gray-400'
                        )}>
                          <Layers className="w-3 h-3" />
                          {message.cacheHit ? 'L2 Cache Hit' : 'No Cache'}
                        </span>
                      )}
                      {message.role === 'assistant' && message.speedup && (
                        <span className="flex items-center gap-1 text-green-600 dark:text-green-400 font-medium">
                          <Zap className="w-3 h-3" />
                          {message.speedup}
                        </span>
                      )}

                      {/* Actions */}
                      <div className={clsx(
                        'flex items-center gap-1 opacity-0 group-hover:opacity-100 transition-opacity',
                        message.role === 'user' ? 'order-first' : ''
                      )}>
                        <button
                          onClick={() => copyMessage(message)}
                          className="p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded"
                        >
                          {copiedMessageId === message.id
                            ? <Check className="w-3.5 h-3.5 text-success-500" />
                            : <Copy className="w-3.5 h-3.5" />
                          }
                        </button>
                        {message.role === 'assistant' && (
                          <>
                            <button
                              onClick={() => handleFeedback(message.id, 'positive')}
                              className={clsx(
                                'p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded transition-colors',
                                message.feedback === 'positive' && 'text-success-500 bg-success-100 dark:bg-success-900/30'
                              )}
                            >
                              <ThumbsUp className="w-3.5 h-3.5" />
                            </button>
                            <button
                              onClick={() => handleFeedback(message.id, 'negative')}
                              className={clsx(
                                'p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded transition-colors',
                                message.feedback === 'negative' && 'text-error-500 bg-error-100 dark:bg-error-900/30'
                              )}
                            >
                              <ThumbsDown className="w-3.5 h-3.5" />
                            </button>
                            <button
                              onClick={() => handleRegenerate(message.id)}
                              className="p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded transition-colors"
                              title="Regenerate response"
                            >
                              <RefreshCw className="w-3.5 h-3.5" />
                            </button>
                          </>
                        )}
                      </div>
                    </div>
                  </div>
                </motion.div>
              ))}

              {/* Typing Indicator / Streaming Display */}
              {(chatMutation.isPending || isStreaming) && (
                <motion.div
                  initial={{ opacity: 0, y: 10 }}
                  animate={{ opacity: 1, y: 0 }}
                  className="flex gap-4"
                >
                  <div className="w-9 h-9 rounded-xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
                    <Bot className="w-5 h-5 text-white" />
                  </div>
                  <div className="flex-1 max-w-[85%]">
                    {isStreaming && streamingText ? (
                      <>
                        {/* Streaming indicator with live metrics */}
                        <div className="flex items-center gap-2 mb-2 flex-wrap">
                          <div className="flex items-center gap-1.5 px-2 py-1 rounded-full bg-green-100 dark:bg-green-900/30 text-green-700 dark:text-green-400 text-xs font-medium">
                            <Radio className="w-3 h-3 animate-pulse" />
                            <span>Streaming</span>
                          </div>
                          {/* Live tok/s */}
                          <div className="flex items-center gap-1 px-2 py-1 rounded-full bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-400 text-xs font-medium">
                            <Zap className="w-3 h-3" />
                            <span>{streamingMetrics.tokensPerSecond.toFixed(1)} tok/s</span>
                          </div>
                          {/* Token count */}
                          <div className="flex items-center gap-1 px-2 py-1 rounded-full bg-surface-100 dark:bg-surface-700 text-surface-600 dark:text-surface-300 text-xs">
                            <span>{streamingMetrics.tokenCount} tokens</span>
                          </div>
                          <button
                            onClick={stopStreaming}
                            className="flex items-center gap-1 px-2 py-1 rounded-full bg-red-100 dark:bg-red-900/30 text-red-700 dark:text-red-400 text-xs font-medium hover:bg-red-200 dark:hover:bg-red-900/50 transition-colors"
                          >
                            <StopCircle className="w-3 h-3" />
                            <span>Stop</span>
                          </button>
                        </div>
                        <div className="bg-surface-100 dark:bg-surface-800 rounded-2xl rounded-tl-md px-4 py-3 max-w-full overflow-hidden">
                          <MarkdownRenderer
                            content={streamingText}
                            className="text-[15px]"
                            isStreaming={true}
                          />
                        </div>
                      </>
                    ) : (
                      <div className="bg-surface-100 dark:bg-surface-800 rounded-2xl rounded-tl-md px-4 py-3">
                        <div className="ai-thinking-dots">
                          <span /><span /><span />
                        </div>
                      </div>
                    )}
                  </div>
                </motion.div>
              )}

              <div ref={messagesEndRef} />
            </div>
          )}
        </div>

        {/* Input Area */}
        <div className="border-t border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900 p-4">
          <div className="max-w-4xl mx-auto">
            <div className="relative">
              <textarea
                ref={inputRef}
                value={input}
                onChange={(e) => setInput(e.target.value)}
                onKeyPress={handleKeyPress}
                placeholder={selectedModel ? 'Type your message... (Shift+Enter for new line)' : 'Select a model to start chatting'}
                disabled={!selectedModel || chatMutation.isPending}
                rows={1}
                className="w-full resize-none rounded-2xl border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-4 py-3 pr-24 text-[15px] placeholder:text-surface-400 focus:outline-none focus:ring-2 focus:ring-brand-500 focus:border-transparent disabled:opacity-50 disabled:cursor-not-allowed"
                style={{ minHeight: '52px', maxHeight: '200px' }}
              />

              <div className="absolute right-2 bottom-2 flex items-center gap-1">
                <Button
                  variant="primary"
                  size="sm"
                  onClick={handleSend}
                  disabled={!input.trim() || !selectedModel || chatMutation.isPending}
                  className="rounded-xl"
                >
                  {chatMutation.isPending ? (
                    <StopCircle className="w-4 h-4" />
                  ) : (
                    <Send className="w-4 h-4" />
                  )}
                </Button>
              </div>
            </div>

            <p className="text-xs text-surface-500 mt-2 text-center">
              Press Enter to send, Shift+Enter for new line
            </p>
          </div>
        </div>
      </div>

      {/* Settings Panel */}
      <AnimatePresence>
        {showSettings && (
          <motion.div
            initial={{ width: 0, opacity: 0 }}
            animate={{ width: 320, opacity: 1 }}
            exit={{ width: 0, opacity: 0 }}
            className="border-l border-surface-200 dark:border-surface-800 bg-white dark:bg-surface-900 overflow-hidden"
          >
            <div className="h-full overflow-y-auto">
              <div className="p-4 border-b border-surface-200 dark:border-surface-800">
                <h3 className="font-semibold text-surface-900 dark:text-white flex items-center gap-2">
                  <Sliders className="w-4 h-4" />
                  Generation Settings
                </h3>
              </div>

              <div className="p-4 space-y-6">
                {/* Presets */}
                <div>
                  <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-3 block">
                    Presets
                  </label>
                  <div className="grid grid-cols-2 gap-2">
                    {Object.entries(PRESETS).map(([key, preset]) => (
                      <button
                        key={key}
                        onClick={() => loadPreset(key)}
                        className={clsx(
                          'flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium transition-all',
                          activePreset === key
                            ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300 ring-1 ring-brand-500'
                            : 'bg-surface-100 dark:bg-surface-800 text-surface-700 dark:text-surface-300 hover:bg-surface-200 dark:hover:bg-surface-700'
                        )}
                      >
                        <preset.icon className="w-4 h-4" />
                        {preset.name}
                      </button>
                    ))}
                  </div>
                </div>

                {/* Max Tokens */}
                <div>
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Max Tokens
                    </label>
                    <span className="text-sm text-surface-500">{settings.max_tokens}</span>
                  </div>
                  <input
                    type="range"
                    min="64"
                    max="8192"
                    step="64"
                    value={settings.max_tokens}
                    onChange={(e) => setSettings(s => ({ ...s, max_tokens: parseInt(e.target.value) }))}
                    className="w-full accent-brand-500"
                  />
                  <p className="text-xs text-surface-500 mt-1">
                    Maximum length of the response
                  </p>
                </div>

                {/* Temperature */}
                <div>
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Temperature
                    </label>
                    <span className="text-sm text-surface-500">{settings.temperature.toFixed(2)}</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="2"
                    step="0.05"
                    value={settings.temperature}
                    onChange={(e) => setSettings(s => ({ ...s, temperature: parseFloat(e.target.value) }))}
                    className="w-full accent-brand-500"
                  />
                  <div className="flex justify-between text-xs text-surface-500 mt-1">
                    <span>Focused</span>
                    <span>Creative</span>
                  </div>
                </div>

                {/* Top P */}
                <div>
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Top P
                    </label>
                    <span className="text-sm text-surface-500">{settings.top_p.toFixed(2)}</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="1"
                    step="0.05"
                    value={settings.top_p}
                    onChange={(e) => setSettings(s => ({ ...s, top_p: parseFloat(e.target.value) }))}
                    className="w-full accent-brand-500"
                  />
                </div>

                {/* Repeat Penalty */}
                <div>
                  <div className="flex justify-between mb-2">
                    <label className="text-sm font-medium text-surface-700 dark:text-surface-300">
                      Repeat Penalty
                    </label>
                    <span className="text-sm text-surface-500">{settings.repeat_penalty.toFixed(2)}</span>
                  </div>
                  <input
                    type="range"
                    min="1.0"
                    max="2.0"
                    step="0.05"
                    value={settings.repeat_penalty}
                    onChange={(e) => setSettings(s => ({ ...s, repeat_penalty: parseFloat(e.target.value) }))}
                    className="w-full accent-brand-500"
                  />
                </div>

                {/* System Prompt */}
                <div>
                  <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                    System Prompt
                  </label>
                  <textarea
                    value={settings.system_prompt}
                    onChange={(e) => setSettings(s => ({ ...s, system_prompt: e.target.value }))}
                    rows={4}
                    className="w-full rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-3 py-2 text-sm resize-none focus:outline-none focus:ring-2 focus:ring-brand-500"
                  />
                </div>

                {/* Advanced Features */}
                <div className="pt-4 border-t border-surface-200 dark:border-surface-800">
                  <h4 className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-3 flex items-center gap-2">
                    <Layers className="w-4 h-4" />
                    Advanced Features
                  </h4>

                  {/* Streaming */}
                  <label className="flex items-center justify-between cursor-pointer mb-4">
                    <div>
                      <span className="text-sm text-surface-600 dark:text-surface-400 flex items-center gap-2">
                        <Radio className="w-4 h-4" />
                        Real-time Streaming
                      </span>
                      <p className="text-xs text-surface-500 mt-0.5">
                        See tokens as they're generated
                      </p>
                    </div>
                    <Toggle
                      checked={settings.use_streaming}
                      onChange={(checked) => setSettings(s => ({ ...s, use_streaming: checked }))}
                    />
                  </label>


                  {/* vPID L2 Context Caching */}
                  <label className="flex items-center justify-between cursor-pointer mb-4">
                    <div>
                      <span className="text-sm text-surface-600 dark:text-surface-400 flex items-center gap-2">
                        <Layers className="w-4 h-4" />
                        vPID L2 Context Cache
                      </span>
                      <p className="text-xs text-surface-500 mt-0.5">
                        O(1) context lookup (faster follow-ups)
                      </p>
                    </div>
                    <Toggle
                      checked={settings.use_context_cache}
                      onChange={(checked) => setSettings(s => ({ ...s, use_context_cache: checked }))}
                    />
                  </label>

                  {/* Extended Thinking */}
                  <label className="flex items-center justify-between cursor-pointer mb-3">
                    <div>
                      <span className="text-sm text-surface-600 dark:text-surface-400 flex items-center gap-2">
                        <Lightbulb className="w-4 h-4" />
                        Extended Thinking
                      </span>
                      <p className="text-xs text-surface-500 mt-0.5">
                        Model shows reasoning steps
                      </p>
                    </div>
                    <Toggle
                      checked={settings.enable_extended_thinking}
                      onChange={(checked) => setSettings(s => ({ ...s, enable_extended_thinking: checked }))}
                    />
                  </label>

                  {/* Thinking Budget (only shown when extended thinking is enabled) */}
                  {settings.enable_extended_thinking && (
                    <div className="ml-6 mb-4">
                      <div className="flex justify-between mb-1">
                        <label className="text-xs font-medium text-surface-600 dark:text-surface-400">
                          Thinking Budget
                        </label>
                        <span className="text-xs text-surface-500">{settings.thinking_budget_tokens} tokens</span>
                      </div>
                      <input
                        type="range"
                        min="256"
                        max="4096"
                        step="256"
                        value={settings.thinking_budget_tokens}
                        onChange={(e) => setSettings(s => ({ ...s, thinking_budget_tokens: parseInt(e.target.value) }))}
                        className="w-full accent-amber-500"
                      />
                    </div>
                  )}
                </div>

                {/* Display Settings */}
                <div className="pt-4 border-t border-surface-200 dark:border-surface-800">
                  <h4 className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-3 flex items-center gap-2">
                    <Eye className="w-4 h-4" />
                    Display
                  </h4>
                  <label className="flex items-center justify-between cursor-pointer">
                    <span className="text-sm text-surface-600 dark:text-surface-400">
                      Filter Chain-of-Thought
                    </span>
                    <Toggle
                      checked={settings.filter_chain_of_thought}
                      onChange={(checked) => setSettings(s => ({ ...s, filter_chain_of_thought: checked }))}
                    />
                  </label>
                  <p className="text-xs text-surface-500 mt-1 ml-0">
                    Hide reasoning tags from display (still visible in Extended Thinking)
                  </p>
                </div>
              </div>
            </div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
}
