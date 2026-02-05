// ============================================================================
// SnapLLM Enterprise - Vision Studio
// Multimodal Image Understanding with Analytics, History & Advanced Features
// ============================================================================

import React, { useState, useCallback, useRef, useEffect, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import { useDropzone } from 'react-dropzone';
import {
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
} from 'recharts';
import {
  Eye,
  Send,
  Image as ImageIcon,
  Upload,
  Copy,
  Check,
  Trash2,
  ZoomIn,
  ZoomOut,
  Loader2,
  ChevronDown,
  History,
  User,
  Bot,
  Sparkles,
  Zap,
  Cpu,
  FileText,
  Scan,
  Search,
  Tag,
  Palette,
  BarChart3,
  Clock,
  Download,
  BookMarked,
  TrendingUp,
  PieChart as PieChartIcon,
  MessageSquare,
  ImagePlus,
  Save,
  Grid3X3,
  List,
  Filter,
  Star,
  RotateCcw,
  ExternalLink,
  Maximize2,
  LayoutGrid,
  ScanEye,
  Type,
} from 'lucide-react';
import { listModels, generateVision } from '../lib/api';
import { useModelStore } from '../store';
import { Button, IconButton, Badge, Card, Progress, Modal } from '../components/ui';
import { MarkdownRenderer } from '../components/ui/MarkdownRenderer';

// ============================================================================
// Types
// ============================================================================

interface VisionMessage {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  image?: string;
  timestamp: Date;
  tokensPerSecond?: number;
  latencyMs?: number;
}

interface AnalysisResult {
  id: string;
  imageUrl: string;
  imageName?: string;
  prompt: string;
  response: string;
  timestamp: Date;
  modelId: string;
  modelName?: string;
  latencyMs?: number;
  tokensPerSecond?: number;
  category?: string;
  starred?: boolean;
}

interface SavedPrompt {
  id: string;
  text: string;
  category: string;
  usageCount: number;
  createdAt: Date;
}

interface VisionStats {
  totalAnalyses: number;
  imagesAnalyzed: number;
  avgResponseTime: number;
  savedPrompts: number;
  categoryDistribution: Record<string, number>;
  dailyAnalyses: { date: string; count: number }[];
}

// ============================================================================
// Constants
// ============================================================================

const ANALYSIS_TEMPLATES = [
  { label: 'Describe', prompt: 'Describe this image in detail.', icon: FileText, category: 'description' },
  { label: 'OCR', prompt: 'Extract all text visible in this image.', icon: Type, category: 'ocr' },
  { label: 'Objects', prompt: 'List all objects you can identify in this image.', icon: Scan, category: 'objects' },
  { label: 'Colors', prompt: 'Analyze the color palette and composition of this image.', icon: Palette, category: 'colors' },
  { label: 'Caption', prompt: 'Write a short, engaging caption for this image.', icon: Tag, category: 'caption' },
  { label: 'Analyze', prompt: 'Provide a comprehensive analysis of this image including objects, context, and meaning.', icon: ScanEye, category: 'analysis' },
];

const CATEGORIES = [
  { id: 'all', label: 'All', icon: Grid3X3 },
  { id: 'description', label: 'Description', icon: FileText },
  { id: 'ocr', label: 'OCR/Text', icon: Type },
  { id: 'objects', label: 'Objects', icon: Scan },
  { id: 'analysis', label: 'Analysis', icon: ScanEye },
  { id: 'custom', label: 'Custom', icon: MessageSquare },
];

const CHART_COLORS = ['#0ea5e9', '#ec4899', '#10b981', '#8b5cf6', '#f59e0b', '#06b6d4'];

// ============================================================================
// Custom Tooltip Component
// ============================================================================

const CustomTooltip = ({ active, payload, label }: any) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white dark:bg-surface-800 border border-surface-200 dark:border-surface-700 rounded-lg shadow-lg p-3">
        <p className="text-sm font-semibold text-surface-900 dark:text-white mb-1">{label}</p>
        {payload.map((entry: any, index: number) => (
          <p key={index} className="text-xs" style={{ color: entry.color }}>
            {entry.name}: {entry.value}
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

export default function Vision() {
  const { models } = useModelStore();
  const messagesEndRef = useRef<HTMLDivElement>(null);

  // State
  const [uploadedImage, setUploadedImage] = useState<string | null>(null);
  const [imageName, setImageName] = useState<string>('');
  const [selectedModel, setSelectedModel] = useState('');
  const [prompt, setPrompt] = useState('');
  const [messages, setMessages] = useState<VisionMessage[]>([]);
  const [analysisHistory, setAnalysisHistory] = useState<AnalysisResult[]>(() => {
    const saved = localStorage.getItem('snapllm_vision_history');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        return parsed.map((r: any) => ({ ...r, timestamp: new Date(r.timestamp) }));
      } catch { return []; }
    }
    return [];
  });
  const [savedPrompts, setSavedPrompts] = useState<SavedPrompt[]>(() => {
    const saved = localStorage.getItem('snapllm_vision_prompts');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        return parsed.map((p: any) => ({ ...p, createdAt: new Date(p.createdAt) }));
      } catch { return []; }
    }
    return [];
  });
  const [isAnalyzing, setIsAnalyzing] = useState(false);
  const [showHistory, setShowHistory] = useState(false);
  const [showAnalytics, setShowAnalytics] = useState(false);
  const [showPromptLibrary, setShowPromptLibrary] = useState(false);
  const [copiedId, setCopiedId] = useState<string | null>(null);
  const [imageZoom, setImageZoom] = useState(100);
  const [analysisPrompt, setAnalysisPrompt] = useState('');
  const [activeTab, setActiveTab] = useState<'chat' | 'history'>('chat');
  const [historyFilter, setHistoryFilter] = useState('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [viewMode, setViewMode] = useState<'grid' | 'list'>('list');
  const [temperature, setTemperature] = useState(0.7);
  const [topP, setTopP] = useState(0.9);
  const [topK, setTopK] = useState(40);
  const [repeatPenalty, setRepeatPenalty] = useState(1.1);

  // Persist to localStorage
  useEffect(() => {
    localStorage.setItem('snapllm_vision_history', JSON.stringify(analysisHistory));
  }, [analysisHistory]);

  useEffect(() => {
    localStorage.setItem('snapllm_vision_prompts', JSON.stringify(savedPrompts));
  }, [savedPrompts]);

  // Scroll to bottom
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages]);

  // Fetch all models to show vision-capable ones
  const { data: modelsResponse } = useQuery({
    queryKey: ['models'],
    queryFn: listModels,
    refetchInterval: 5000,
  });

  // Vision-capable = type 'vision' OR LLM models with known multimodal architectures
  const VISION_CAPABLE_PATTERNS = [
    'gemma', 'llava', 'qwen-vl', 'qwen2-vl', 'cogvlm', 'internvl',
    'minicpm-v', 'phi-3-vision', 'moondream', 'bakllava', 'obsidian'
  ];

  const isVisionCapable = (model: any) => {
    if (model.type === 'vision') return true;
    if (model.type !== 'llm') return false;
    const nameLower = model.name?.toLowerCase() || model.id?.toLowerCase() || '';
    return VISION_CAPABLE_PATTERNS.some(pattern => nameLower.includes(pattern));
  };

  const visionModels = modelsResponse?.models?.filter(
    (m: any) => m.status === 'loaded' && isVisionCapable(m)
  ) || [];

  // Calculate statistics
  const stats = useMemo((): VisionStats => {
    const uniqueImages = new Set(analysisHistory.map(a => a.imageUrl)).size;
    const totalTime = analysisHistory.reduce((sum, a) => sum + (a.latencyMs || 0), 0);
    const avgTime = analysisHistory.length > 0 ? totalTime / analysisHistory.length : 0;

    // Category distribution
    const categoryDistribution: Record<string, number> = {};
    analysisHistory.forEach(a => {
      const cat = a.category || 'custom';
      categoryDistribution[cat] = (categoryDistribution[cat] || 0) + 1;
    });

    // Daily analyses (last 7 days)
    const dailyAnalyses: { date: string; count: number }[] = [];
    const now = new Date();
    for (let i = 6; i >= 0; i--) {
      const date = new Date(now);
      date.setDate(date.getDate() - i);
      const dateStr = date.toLocaleDateString('en-US', { weekday: 'short' });
      const count = analysisHistory.filter(a => {
        const aDate = new Date(a.timestamp);
        return aDate.toDateString() === date.toDateString();
      }).length;
      dailyAnalyses.push({ date: dateStr, count });
    }

    return {
      totalAnalyses: analysisHistory.length,
      imagesAnalyzed: uniqueImages,
      avgResponseTime: avgTime,
      savedPrompts: savedPrompts.length,
      categoryDistribution,
      dailyAnalyses,
    };
  }, [analysisHistory, savedPrompts]);

  // Filtered history
  const filteredHistory = useMemo(() => {
    let history = [...analysisHistory];

    if (historyFilter !== 'all') {
      history = history.filter(a => a.category === historyFilter);
    }

    if (searchQuery) {
      history = history.filter(a =>
        a.prompt.toLowerCase().includes(searchQuery.toLowerCase()) ||
        a.response.toLowerCase().includes(searchQuery.toLowerCase())
      );
    }

    return history.sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime());
  }, [analysisHistory, historyFilter, searchQuery]);

  // Chart data
  const categoryChartData = useMemo(() => {
    return Object.entries(stats.categoryDistribution).map(([name, value], i) => ({
      name: CATEGORIES.find(c => c.id === name)?.label || name,
      value,
      fill: CHART_COLORS[i % CHART_COLORS.length],
    }));
  }, [stats.categoryDistribution]);

  // Dropzone
  const onDrop = useCallback((acceptedFiles: File[]) => {
    const file = acceptedFiles[0];
    if (file) {
      setImageName(file.name);
      const reader = new FileReader();
      reader.onload = () => {
        setUploadedImage(reader.result as string);
        setImageZoom(100);
      };
      reader.readAsDataURL(file);
    }
  }, []);

  const { getRootProps, getInputProps, isDragActive } = useDropzone({
    onDrop,
    accept: {
      'image/*': ['.png', '.jpg', '.jpeg', '.webp', '.gif'],
    },
    maxFiles: 1,
  });

  // Analyze image
  const handleAnalyze = async (templatePrompt?: string) => {
    const actualPrompt = templatePrompt || prompt;
    if (!actualPrompt.trim() || !uploadedImage || !selectedModel) return;

    setIsAnalyzing(true);
    setAnalysisPrompt(actualPrompt);

    // Add user message
    const userMessage: VisionMessage = {
      id: crypto.randomUUID(),
      role: 'user',
      content: actualPrompt,
      image: messages.length === 0 ? uploadedImage : undefined,
      timestamp: new Date(),
    };
    setMessages(prev => [...prev, userMessage]);
    setPrompt('');

    try {
      const startTime = Date.now();
      const base64Image = uploadedImage.split(',')[1] || uploadedImage;

      const response = await generateVision({
        model: selectedModel,
        images: [base64Image],
        prompt: actualPrompt,
        max_tokens: 1024,
        temperature,
        top_p: topP,
        top_k: topK,
        repeat_penalty: repeatPenalty,
      });

      const latencyMs = Date.now() - startTime;
      const model = visionModels.find((m: any) => m.id === selectedModel);

      const assistantMessage: VisionMessage = {
        id: crypto.randomUUID(),
        role: 'assistant',
        content: response.response,
        timestamp: new Date(),
        tokensPerSecond: response.tokens_per_second,
        latencyMs,
      };
      setMessages(prev => [...prev, assistantMessage]);

      // Determine category
      const template = ANALYSIS_TEMPLATES.find(t => t.prompt === actualPrompt);
      const category = template?.category || 'custom';

      // Add to history
      const result: AnalysisResult = {
        id: crypto.randomUUID(),
        imageUrl: uploadedImage,
        imageName: imageName,
        prompt: actualPrompt,
        response: assistantMessage.content,
        timestamp: new Date(),
        modelId: selectedModel,
        modelName: model?.name || selectedModel,
        latencyMs,
        tokensPerSecond: response.tokens_per_second,
        category,
        starred: false,
      };
      setAnalysisHistory(prev => [result, ...prev]);
    } catch (error: any) {
      const errorMessage: VisionMessage = {
        id: crypto.randomUUID(),
        role: 'assistant',
        content: `Error: ${error.response?.data?.error?.message || error.message || 'Vision analysis failed. Make sure a vision-capable model is loaded.'}`,
        timestamp: new Date(),
      };
      setMessages(prev => [...prev, errorMessage]);
    } finally {
      setIsAnalyzing(false);
    }
  };

  // Copy message
  const copyMessage = (message: VisionMessage) => {
    navigator.clipboard.writeText(message.content);
    setCopiedId(message.id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  // Clear image
  const clearImage = () => {
    setUploadedImage(null);
    setImageName('');
    setMessages([]);
    setPrompt('');
  };

  // Toggle star
  const toggleStar = (id: string) => {
    setAnalysisHistory(prev =>
      prev.map(a => a.id === id ? { ...a, starred: !a.starred } : a)
    );
  };

  // Delete analysis
  const deleteAnalysis = (id: string) => {
    setAnalysisHistory(prev => prev.filter(a => a.id !== id));
  };

  // Save prompt
  const savePrompt = () => {
    if (!prompt.trim()) return;
    const newPrompt: SavedPrompt = {
      id: crypto.randomUUID(),
      text: prompt,
      category: 'custom',
      usageCount: 0,
      createdAt: new Date(),
    };
    setSavedPrompts(prev => [newPrompt, ...prev]);
  };

  // Use saved prompt
  const useSavedPrompt = (saved: SavedPrompt) => {
    setPrompt(saved.text);
    setSavedPrompts(prev =>
      prev.map(p => p.id === saved.id ? { ...p, usageCount: p.usageCount + 1 } : p)
    );
    setShowPromptLibrary(false);
  };

  // Export history
  const exportHistory = () => {
    const data = {
      analyses: analysisHistory,
      prompts: savedPrompts,
      exportedAt: new Date().toISOString(),
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `snapllm-vision-history-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  // Load from history
  const loadFromHistory = (result: AnalysisResult) => {
    setUploadedImage(result.imageUrl);
    setImageName(result.imageName || '');
    setMessages([
      {
        id: crypto.randomUUID(),
        role: 'user',
        content: result.prompt,
        image: result.imageUrl,
        timestamp: result.timestamp,
      },
      {
        id: crypto.randomUUID(),
        role: 'assistant',
        content: result.response,
        timestamp: result.timestamp,
        tokensPerSecond: result.tokensPerSecond,
        latencyMs: result.latencyMs,
      },
    ]);
    setActiveTab('chat');
  };

  // Handle key press
  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleAnalyze();
    }
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-cyan-500 to-blue-600 flex items-center justify-center">
            <Eye className="w-6 h-6 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
              Vision Studio
            </h1>
            <p className="text-surface-500">
              Multimodal image understanding and analysis
            </p>
          </div>
        </div>

        <div className="flex items-center gap-3">
          {/* Model Selector */}
          <div className="relative">
            <select
              value={selectedModel}
              onChange={(e) => setSelectedModel(e.target.value)}
              className="appearance-none pl-10 pr-8 py-2 rounded-xl bg-surface-100 dark:bg-surface-800 border border-surface-200 dark:border-surface-700 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-brand-500 min-w-[200px]"
            >
              <option value="">Select model...</option>
              {visionModels.map((model: any) => (
                <option key={model.id} value={model.id}>
                  {model.name} {model.type === 'vision' ? '✓' : '(needs mmproj)'}
                </option>
              ))}
            </select>
            <Cpu className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
            <ChevronDown className="absolute right-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
          </div>

          {/* Warning for models needing mmproj */}
          {selectedModel && visionModels.find((m: any) => m.id === selectedModel)?.type !== 'vision' && (
            <div className="px-3 py-1.5 rounded-lg bg-warning-100 dark:bg-warning-900/30 text-warning-700 dark:text-warning-300 text-xs">
              Reload with mmproj for vision
            </div>
          )}

          <Button variant="secondary" onClick={() => setShowAnalytics(!showAnalytics)}>
            <BarChart3 className={clsx('w-4 h-4', showAnalytics && 'text-brand-500')} />
            Analytics
          </Button>
          <Button variant="secondary" onClick={exportHistory}>
            <Download className="w-4 h-4" />
            Export
          </Button>
        </div>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <ScanEye className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {stats.totalAnalyses}
              </p>
              <p className="text-sm text-surface-500">Total Analyses</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <ImagePlus className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {stats.imagesAnalyzed}
              </p>
              <p className="text-sm text-surface-500">Images Analyzed</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Clock className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {(stats.avgResponseTime / 1000).toFixed(1)}s
              </p>
              <p className="text-sm text-surface-500">Avg Response Time</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
              <BookMarked className="w-5 h-5 text-ai-purple" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {stats.savedPrompts}
              </p>
              <p className="text-sm text-surface-500">Saved Prompts</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Sampling Controls */}
      <Card className="p-4">
        <div className="grid grid-cols-4 gap-4">
          <div className="space-y-1">
            <label className="text-xs font-semibold text-surface-500">Temperature</label>
            <input
              type="number"
              min={0}
              max={2}
              step={0.1}
              value={temperature}
              onChange={(e) => setTemperature(Number(e.target.value))}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm"
            />
          </div>
          <div className="space-y-1">
            <label className="text-xs font-semibold text-surface-500">Top P</label>
            <input
              type="number"
              min={0}
              max={1}
              step={0.05}
              value={topP}
              onChange={(e) => setTopP(Number(e.target.value))}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm"
            />
          </div>
          <div className="space-y-1">
            <label className="text-xs font-semibold text-surface-500">Top K</label>
            <input
              type="number"
              min={0}
              max={200}
              step={1}
              value={topK}
              onChange={(e) => setTopK(Number(e.target.value))}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm"
            />
          </div>
          <div className="space-y-1">
            <label className="text-xs font-semibold text-surface-500">Repeat Penalty</label>
            <input
              type="number"
              min={0.8}
              max={1.5}
              step={0.05}
              value={repeatPenalty}
              onChange={(e) => setRepeatPenalty(Number(e.target.value))}
              className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm"
            />
          </div>
        </div>
      </Card>

      {/* Analytics Section */}
      <AnimatePresence>
        {showAnalytics && (
          <motion.div
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            className="grid grid-cols-2 gap-6"
          >
            {/* Analysis Trend Chart */}
            <Card className="p-6">
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <TrendingUp className="w-5 h-5 text-brand-500" />
                Analysis Trend (7 Days)
              </h3>
              <div className="h-48">
                <ResponsiveContainer width="100%" height="100%">
                  <BarChart data={stats.dailyAnalyses}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
                    <XAxis dataKey="date" tick={{ fontSize: 12 }} />
                    <YAxis tick={{ fontSize: 12 }} />
                    <Tooltip content={<CustomTooltip />} />
                    <Bar dataKey="count" name="Analyses" fill="#06b6d4" radius={[4, 4, 0, 0]} />
                  </BarChart>
                </ResponsiveContainer>
              </div>
            </Card>

            {/* Category Distribution Chart */}
            <Card className="p-6">
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <PieChartIcon className="w-5 h-5 text-ai-purple" />
                Analysis Categories
              </h3>
              <div className="h-48">
                {categoryChartData.length > 0 ? (
                  <ResponsiveContainer width="100%" height="100%">
                    <PieChart>
                      <Pie
                        data={categoryChartData}
                        cx="50%"
                        cy="50%"
                        outerRadius={60}
                        dataKey="value"
                        label={({ name, percent }) => `${name} (${(percent * 100).toFixed(0)}%)`}
                        labelLine={false}
                      >
                        {categoryChartData.map((entry, index) => (
                          <Cell key={`cell-${index}`} fill={entry.fill} />
                        ))}
                      </Pie>
                      <Tooltip />
                    </PieChart>
                  </ResponsiveContainer>
                ) : (
                  <div className="h-full flex items-center justify-center text-surface-500">
                    <p>No data yet</p>
                  </div>
                )}
              </div>
            </Card>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Tab Navigation */}
      <div className="flex items-center gap-2">
        {[
          { id: 'chat', label: 'Analysis', icon: Eye },
          { id: 'history', label: 'History', icon: History },
        ].map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id as any)}
            className={clsx(
              'flex items-center gap-2 px-4 py-2 rounded-lg text-sm font-medium transition-all',
              activeTab === tab.id
                ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300'
                : 'text-surface-600 dark:text-surface-400 hover:bg-surface-100 dark:hover:bg-surface-800'
            )}
          >
            <tab.icon className="w-4 h-4" />
            {tab.label}
            {tab.id === 'history' && (
              <Badge variant="default">{analysisHistory.length}</Badge>
            )}
          </button>
        ))}
      </div>

      {/* Chat Tab */}
      {activeTab === 'chat' && (
        <div className="grid grid-cols-2 gap-6 h-[600px]">
          {/* Image Panel */}
          <Card className="flex flex-col overflow-hidden">
            {uploadedImage ? (
              <>
                {/* Image Preview */}
                <div className="flex-1 relative overflow-hidden bg-surface-100 dark:bg-surface-800 flex items-center justify-center">
                  <img
                    src={uploadedImage}
                    alt="Uploaded"
                    className="max-w-full max-h-full object-contain transition-transform"
                    style={{ transform: `scale(${imageZoom / 100})` }}
                  />

                  {/* Image Controls */}
                  <div className="absolute top-4 right-4 flex items-center gap-2">
                    <div className="flex items-center gap-1 bg-white/90 dark:bg-surface-900/90 backdrop-blur-sm rounded-lg p-1">
                      <IconButton
                        icon={<ZoomOut className="w-4 h-4" />}
                        label="Zoom out"
                        onClick={() => setImageZoom(z => Math.max(25, z - 25))}
                      />
                      <span className="text-xs font-medium w-12 text-center">{imageZoom}%</span>
                      <IconButton
                        icon={<ZoomIn className="w-4 h-4" />}
                        label="Zoom in"
                        onClick={() => setImageZoom(z => Math.min(200, z + 25))}
                      />
                    </div>
                    <IconButton
                      icon={<Trash2 className="w-4 h-4" />}
                      label="Remove"
                      onClick={clearImage}
                      className="bg-white/90 dark:bg-surface-900/90 backdrop-blur-sm"
                    />
                  </div>

                  {/* Image Name */}
                  {imageName && (
                    <div className="absolute bottom-4 left-4 bg-white/90 dark:bg-surface-900/90 backdrop-blur-sm rounded-lg px-3 py-1.5">
                      <p className="text-xs font-medium text-surface-700 dark:text-surface-300">{imageName}</p>
                    </div>
                  )}
                </div>

                {/* Quick Actions */}
                <div className="p-4 border-t border-surface-200 dark:border-surface-700">
                  <p className="text-xs font-medium text-surface-500 mb-3">QUICK ANALYSIS</p>
                  <div className="flex flex-wrap gap-2">
                    {ANALYSIS_TEMPLATES.map((template) => (
                      <Button
                        key={template.label}
                        variant="secondary"
                        size="sm"
                        onClick={() => handleAnalyze(template.prompt)}
                        disabled={!selectedModel || isAnalyzing}
                      >
                        <template.icon className="w-4 h-4" />
                        {template.label}
                      </Button>
                    ))}
                  </div>
                </div>
              </>
            ) : (
              // Upload Area
              <div
                {...getRootProps()}
                className={clsx(
                  'flex-1 flex flex-col items-center justify-center p-8 cursor-pointer transition-colors',
                  isDragActive
                    ? 'bg-brand-50 dark:bg-brand-900/20'
                    : 'bg-surface-50 dark:bg-surface-800 hover:bg-surface-100 dark:hover:bg-surface-700'
                )}
              >
                <input {...getInputProps()} />
                <div className={clsx(
                  'w-20 h-20 rounded-2xl flex items-center justify-center mb-4',
                  isDragActive ? 'bg-brand-100 dark:bg-brand-900/40' : 'bg-surface-100 dark:bg-surface-800'
                )}>
                  {isDragActive ? (
                    <Upload className="w-10 h-10 text-brand-500" />
                  ) : (
                    <ImageIcon className="w-10 h-10 text-surface-400" />
                  )}
                </div>
                <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
                  {isDragActive ? 'Drop your image here' : 'Upload an Image'}
                </h3>
                <p className="text-surface-500 text-center text-sm max-w-xs mb-4">
                  Drag and drop an image here, or click to browse.
                  Supports PNG, JPG, WebP, and GIF.
                </p>
                <Button variant="secondary">
                  <Upload className="w-4 h-4" />
                  Browse Files
                </Button>
              </div>
            )}
          </Card>

          {/* Chat Panel */}
          <Card className="flex flex-col overflow-hidden">
            {/* Messages */}
            <div className="flex-1 overflow-y-auto p-4 space-y-4">
              {messages.length === 0 ? (
                <div className="h-full flex flex-col items-center justify-center text-center">
                  <div className="w-16 h-16 rounded-2xl bg-gradient-to-br from-cyan-500/20 to-blue-600/20 flex items-center justify-center mb-4">
                    <Sparkles className="w-8 h-8 text-cyan-500" />
                  </div>
                  <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
                    Ask About Your Image
                  </h3>
                  <p className="text-surface-500 text-sm max-w-xs">
                    {uploadedImage
                      ? 'Use the quick actions or type a custom question below.'
                      : 'Upload an image to start analyzing it with AI.'}
                  </p>
                </div>
              ) : (
                messages.map((message) => (
                  <motion.div
                    key={message.id}
                    initial={{ opacity: 0, y: 10 }}
                    animate={{ opacity: 1, y: 0 }}
                    className={clsx('flex gap-3', message.role === 'user' ? 'flex-row-reverse' : '')}
                  >
                    {/* Avatar */}
                    <div className={clsx(
                      'w-8 h-8 rounded-lg flex items-center justify-center flex-shrink-0',
                      message.role === 'user'
                        ? 'bg-gradient-to-br from-surface-700 to-surface-900'
                        : 'bg-gradient-to-br from-cyan-500 to-blue-600'
                    )}>
                      {message.role === 'user' ? <User className="w-4 h-4 text-white" /> : <Bot className="w-4 h-4 text-white" />}
                    </div>

                    {/* Content */}
                    <div className={clsx('flex-1 max-w-[85%]', message.role === 'user' ? 'text-right' : '')}>
                      {message.image && (
                        <div className={clsx('mb-2', message.role === 'user' ? 'flex justify-end' : '')}>
                          <img src={message.image} alt="" className="w-24 h-24 rounded-lg object-cover" />
                        </div>
                      )}
                      <div className={clsx(
                        'inline-block rounded-2xl px-4 py-3 max-w-full',
                        message.role === 'user'
                          ? 'bg-brand-600 text-white rounded-tr-md'
                          : 'bg-white dark:bg-surface-800 text-surface-900 dark:text-surface-100 rounded-tl-md shadow-sm'
                      )}>
                        {message.role === 'user' ? (
                          <p className="whitespace-pre-wrap text-sm leading-relaxed">{message.content}</p>
                        ) : (
                          <MarkdownRenderer content={message.content} className="text-sm" />
                        )}
                      </div>

                      {/* Meta */}
                      <div className={clsx('flex items-center gap-2 mt-1 text-xs text-surface-500', message.role === 'user' ? 'justify-end' : '')}>
                        <span>{message.timestamp.toLocaleTimeString()}</span>
                        {message.tokensPerSecond && (
                          <span className="flex items-center gap-1">
                            <Zap className="w-3 h-3" />{message.tokensPerSecond.toFixed(1)} tok/s
                          </span>
                        )}
                        {message.latencyMs && (
                          <span className="flex items-center gap-1">
                            <Clock className="w-3 h-3" />{(message.latencyMs / 1000).toFixed(1)}s
                          </span>
                        )}
                        {message.role === 'assistant' && (
                          <button onClick={() => copyMessage(message)} className="p-1 hover:bg-surface-200 dark:hover:bg-surface-700 rounded">
                            {copiedId === message.id ? <Check className="w-3 h-3 text-success-500" /> : <Copy className="w-3 h-3" />}
                          </button>
                        )}
                      </div>
                    </div>
                  </motion.div>
                ))
              )}

              {/* Loading indicator */}
              {isAnalyzing && (
                <motion.div initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} className="flex gap-3">
                  <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-cyan-500 to-blue-600 flex items-center justify-center">
                    <Bot className="w-4 h-4 text-white" />
                  </div>
                  <div className="bg-white dark:bg-surface-800 rounded-2xl rounded-tl-md px-4 py-3 shadow-sm">
                    <div className="flex items-center gap-2">
                      <Loader2 className="w-4 h-4 animate-spin text-brand-500" />
                      <span className="text-sm text-surface-500">Analyzing image...</span>
                    </div>
                  </div>
                </motion.div>
              )}

              <div ref={messagesEndRef} />
            </div>

            {/* Input */}
            <div className="p-4 border-t border-surface-200 dark:border-surface-700">
              <div className="flex gap-2">
                <div className="flex-1 flex gap-2">
                  <textarea
                    value={prompt}
                    onChange={(e) => setPrompt(e.target.value)}
                    onKeyPress={handleKeyPress}
                    placeholder={uploadedImage ? 'Ask about this image...' : 'Upload an image first'}
                    disabled={!uploadedImage || !selectedModel || isAnalyzing}
                    rows={1}
                    className="flex-1 resize-none rounded-xl border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-4 py-3 text-sm placeholder:text-surface-400 focus:outline-none focus:ring-2 focus:ring-brand-500 disabled:opacity-50"
                  />
                  <IconButton icon={<Save className="w-4 h-4" />} label="Save Prompt" onClick={savePrompt} />
                  <IconButton icon={<BookMarked className="w-4 h-4" />} label="Prompt Library" onClick={() => setShowPromptLibrary(true)} />
                </div>
                <Button variant="primary" onClick={() => handleAnalyze()} disabled={!prompt.trim() || !uploadedImage || !selectedModel || isAnalyzing}>
                  <Send className="w-4 h-4" />
                </Button>
              </div>
            </div>
          </Card>
        </div>
      )}

      {/* History Tab */}
      {activeTab === 'history' && (
        <div className="space-y-4">
          {/* Filters */}
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              {CATEGORIES.map(cat => (
                <button
                  key={cat.id}
                  onClick={() => setHistoryFilter(cat.id)}
                  className={clsx(
                    'flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-sm font-medium transition-all',
                    historyFilter === cat.id
                      ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300'
                      : 'text-surface-500 hover:bg-surface-100 dark:hover:bg-surface-800'
                  )}
                >
                  <cat.icon className="w-3.5 h-3.5" />
                  {cat.label}
                </button>
              ))}
            </div>

            <div className="flex items-center gap-2">
              {/* Search */}
              <div className="relative">
                <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
                <input
                  type="text"
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="Search..."
                  className="pl-9 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm w-48"
                />
              </div>

              {/* View Mode */}
              <div className="flex items-center border border-surface-200 dark:border-surface-700 rounded-lg overflow-hidden">
                <button onClick={() => setViewMode('grid')} className={clsx('p-2', viewMode === 'grid' ? 'bg-brand-100 text-brand-600' : 'text-surface-500')}>
                  <LayoutGrid className="w-4 h-4" />
                </button>
                <button onClick={() => setViewMode('list')} className={clsx('p-2', viewMode === 'list' ? 'bg-brand-100 text-brand-600' : 'text-surface-500')}>
                  <List className="w-4 h-4" />
                </button>
              </div>
            </div>
          </div>

          {/* History List */}
          {filteredHistory.length > 0 ? (
            <div className={clsx(viewMode === 'grid' ? 'grid grid-cols-3 gap-4' : 'space-y-3')}>
              {filteredHistory.map((result) => (
                <Card
                  key={result.id}
                  className={clsx('overflow-hidden cursor-pointer hover:ring-2 hover:ring-brand-500/50 transition-all', viewMode === 'list' && 'flex')}
                  onClick={() => loadFromHistory(result)}
                >
                  {viewMode === 'grid' ? (
                    <>
                      <div className="aspect-video overflow-hidden bg-surface-100 dark:bg-surface-800">
                        <img src={result.imageUrl} alt="" className="w-full h-full object-cover" />
                      </div>
                      <div className="p-3">
                        <div className="flex items-center gap-2 mb-2">
                          <Badge variant="default">{result.category || 'custom'}</Badge>
                          {result.starred && <Star className="w-3 h-3 fill-warning-500 text-warning-500" />}
                        </div>
                        <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-2 mb-2">{result.prompt}</p>
                        <p className="text-xs text-surface-500">{new Date(result.timestamp).toLocaleString()}</p>
                      </div>
                    </>
                  ) : (
                    <>
                      <img src={result.imageUrl} alt="" className="w-20 h-20 object-cover flex-shrink-0" />
                      <div className="flex-1 p-3">
                        <div className="flex items-center gap-2 mb-1">
                          <Badge variant="default">{result.category || 'custom'}</Badge>
                          <span className="text-xs text-surface-500">{result.modelName || result.modelId}</span>
                          {result.starred && <Star className="w-3 h-3 fill-warning-500 text-warning-500" />}
                        </div>
                        <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-1">{result.prompt}</p>
                        <p className="text-xs text-surface-500 line-clamp-1 mt-1">{result.response}</p>
                      </div>
                      <div className="flex flex-col justify-center gap-1 px-3">
                        <IconButton
                          icon={<Star className={clsx('w-4 h-4', result.starred && 'fill-warning-500 text-warning-500')} />}
                          label="Star"
                          onClick={(e) => { e.stopPropagation(); toggleStar(result.id); }}
                        />
                        <IconButton
                          icon={<Trash2 className="w-4 h-4 text-error-500" />}
                          label="Delete"
                          onClick={(e) => { e.stopPropagation(); deleteAnalysis(result.id); }}
                        />
                      </div>
                    </>
                  )}
                </Card>
              ))}
            </div>
          ) : (
            <Card className="p-12 text-center">
              <div className="w-20 h-20 rounded-2xl bg-surface-100 dark:bg-surface-800 flex items-center justify-center mx-auto mb-6">
                <History className="w-10 h-10 text-surface-400" />
              </div>
              <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
                No Analysis History
              </h2>
              <p className="text-surface-500 max-w-md mx-auto">
                Your image analyses will appear here. Upload an image and ask questions to get started.
              </p>
            </Card>
          )}
        </div>
      )}

      {/* Prompt Library Modal */}
      <Modal isOpen={showPromptLibrary} onClose={() => setShowPromptLibrary(false)} title="Prompt Library" size="lg">
        <div className="space-y-4">
          {savedPrompts.length > 0 ? (
            savedPrompts.map(saved => (
              <div
                key={saved.id}
                className="flex items-start gap-3 p-3 rounded-lg bg-surface-50 dark:bg-surface-800 hover:bg-surface-100 dark:hover:bg-surface-700 transition-colors"
              >
                <div className="flex-1">
                  <p className="text-sm text-surface-700 dark:text-surface-300">{saved.text}</p>
                  <div className="flex items-center gap-2 mt-1 text-xs text-surface-500">
                    <span>Used {saved.usageCount} times</span>
                    <span>•</span>
                    <span>{new Date(saved.createdAt).toLocaleDateString()}</span>
                  </div>
                </div>
                <Button variant="primary" size="sm" onClick={() => useSavedPrompt(saved)}>
                  Use
                </Button>
              </div>
            ))
          ) : (
            <div className="text-center py-8">
              <BookMarked className="w-12 h-12 text-surface-300 mx-auto mb-3" />
              <p className="text-surface-500">No saved prompts yet</p>
              <p className="text-sm text-surface-400">Save prompts to reuse them later</p>
            </div>
          )}
        </div>
      </Modal>
    </div>
  );
}
