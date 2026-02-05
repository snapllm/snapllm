// ============================================================================
// SnapLLM Enterprise - Image Studio
// AI Image Generation with Stable Diffusion, Gallery Management & Analytics
// ============================================================================

import { useState, useMemo, useEffect, useRef } from 'react';
import { useQuery, useMutation } from '@tanstack/react-query';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
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
  Image as ImageIcon,
  Wand2,
  Download,
  Copy,
  Trash2,
  RefreshCw,
  Sparkles,
  SlidersHorizontal,
  Clock,
  Star,
  ChevronDown,
  Plus,
  Minus,
  Square,
  Save,
  Check,
  Loader2,
  Cpu,
  BarChart3,
  PieChart as PieChartIcon,
  TrendingUp,
  BookMarked,
  Search,
  LayoutGrid,
  List,
  CheckSquare,
  ImagePlus,
  FileImage,
  AlertTriangle,
} from 'lucide-react';
import { generateImage, listDiffusionModels } from '../lib/api';
import { useModelStore } from '../store';
import { Button, IconButton, Badge, Card, Progress, Modal } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface GeneratedImage {
  id: string;
  url: string;
  prompt: string;
  negativePrompt?: string;
  width: number;
  height: number;
  steps: number;
  cfgScale: number;
  seed: number;
  modelId: string;
  modelName?: string;
  sampler: string;
  createdAt: Date;
  generationTime: number;
  starred: boolean;
  tags?: string[];
}

interface ImageSettings {
  width: number;
  height: number;
  steps: number;
  cfgScale: number;
  seed: number;
  sampler: string;
  negativePrompt: string;
  batchSize: number;
}

interface SavedPrompt {
  id: string;
  text: string;
  negativePrompt?: string;
  settings?: Partial<ImageSettings>;
  usageCount: number;
  createdAt: Date;
}

interface GenerationStats {
  totalGenerated: number;
  totalFavorites: number;
  avgGenerationTime: number;
  totalTime: number;
  styleDistribution: Record<string, number>;
  dailyGenerations: { date: string; count: number }[];
}

// ============================================================================
// Constants
// ============================================================================

const ASPECT_RATIOS = [
  { label: '1:1', width: 512, height: 512, icon: Square },
  { label: '4:3', width: 512, height: 384, icon: Square },
  { label: '16:9', width: 512, height: 288, icon: Square },
  { label: '9:16', width: 288, height: 512, icon: Square },
  { label: '3:4', width: 384, height: 512, icon: Square },
];

const SAMPLERS = [
  'Euler', 'Euler a', 'LMS', 'Heun', 'DPM2', 'DPM2 a',
  'DPM++ 2S a', 'DPM++ 2M', 'DPM++ SDE', 'DPM fast', 'DPM adaptive',
  'LMS Karras', 'DPM2 Karras', 'DPM2 a Karras', 'DPM++ 2S a Karras',
  'DPM++ 2M Karras', 'DPM++ SDE Karras', 'DDIM', 'PLMS',
];

const STYLE_PRESETS = [
  { name: 'Photorealistic', prompt: 'photorealistic, highly detailed, 8k, professional photography', color: '#0ea5e9' },
  { name: 'Anime', prompt: 'anime style, detailed anime art, studio ghibli, makoto shinkai', color: '#ec4899' },
  { name: 'Oil Painting', prompt: 'oil painting, classical art, museum quality, masterpiece', color: '#10b981' },
  { name: 'Watercolor', prompt: 'watercolor painting, soft colors, artistic, flowing', color: '#8b5cf6' },
  { name: 'Digital Art', prompt: 'digital art, concept art, artstation trending, highly detailed', color: '#f59e0b' },
  { name: '3D Render', prompt: '3D render, octane render, cinema 4d, unreal engine 5', color: '#06b6d4' },
  { name: 'Cyberpunk', prompt: 'cyberpunk style, neon lights, futuristic, sci-fi', color: '#ef4444' },
  { name: 'Fantasy', prompt: 'fantasy art, magical, ethereal, greg rutkowski style', color: '#a855f7' },
];

const CHART_COLORS = ['#0ea5e9', '#ec4899', '#10b981', '#8b5cf6', '#f59e0b', '#06b6d4', '#ef4444', '#a855f7'];

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

export default function Images() {
  const { models: _models } = useModelStore();

  // State
  const [prompt, setPrompt] = useState('');
  const [selectedModel, setSelectedModel] = useState('');
  const [generatedImages, setGeneratedImages] = useState<GeneratedImage[]>(() => {
    // Load from localStorage
    const saved = localStorage.getItem('snapllm_generated_images');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        return parsed.map((img: any) => ({ ...img, createdAt: new Date(img.createdAt) }));
      } catch { return []; }
    }
    return [];
  });
  const [savedPrompts, setSavedPrompts] = useState<SavedPrompt[]>(() => {
    const saved = localStorage.getItem('snapllm_saved_prompts');
    if (saved) {
      try {
        const parsed = JSON.parse(saved);
        return parsed.map((p: any) => ({ ...p, createdAt: new Date(p.createdAt) }));
      } catch { return []; }
    }
    return [];
  });
  const [selectedImage, setSelectedImage] = useState<GeneratedImage | null>(null);
  const [selectedImages, setSelectedImages] = useState<Set<string>>(new Set());
  const [showAnalytics, setShowAnalytics] = useState(false);
  const progressIntervalRef = useRef<NodeJS.Timeout | null>(null);
  const [showPromptLibrary, setShowPromptLibrary] = useState(false);
  const [isGenerating, setIsGenerating] = useState(false);
  const [generationProgress, setGenerationProgress] = useState(0);
  const [generationError, setGenerationError] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<'gallery' | 'favorites' | 'recent'>('gallery');
  const [viewMode, setViewMode] = useState<'grid' | 'list'>('grid');
  const [searchQuery, setSearchQuery] = useState('');
  const [isSelectMode, setIsSelectMode] = useState(false);
  const [showImageModal, setShowImageModal] = useState(false);

  const [settings, setSettings] = useState<ImageSettings>({
    width: 512,
    height: 512,
    steps: 25,
    cfgScale: 7.5,
    seed: -1,
    sampler: 'DPM++ 2M Karras',
    negativePrompt: 'blurry, bad quality, worst quality, low quality, distorted',
    batchSize: 1,
  });

  // Persist to localStorage
  useEffect(() => {
    localStorage.setItem('snapllm_generated_images', JSON.stringify(generatedImages));
  }, [generatedImages]);

  useEffect(() => {
    localStorage.setItem('snapllm_saved_prompts', JSON.stringify(savedPrompts));
  }, [savedPrompts]);

  // Fetch diffusion models (filtered by type)
  const { data: diffusionModelsResponse } = useQuery({
    queryKey: ['models', 'diffusion'],
    queryFn: listDiffusionModels,
    refetchInterval: 5000,
  });

  // Diffusion models only
  const diffusionModels = diffusionModelsResponse?.models?.filter((m: any) => m.status === 'loaded') || [];

  // Calculate statistics
  const stats = useMemo((): GenerationStats => {
    const favorites = generatedImages.filter(img => img.starred).length;
    const totalTime = generatedImages.reduce((sum, img) => sum + img.generationTime, 0);
    const avgTime = generatedImages.length > 0 ? totalTime / generatedImages.length : 0;

    // Style distribution (based on prompt keywords)
    const styleDistribution: Record<string, number> = {};
    STYLE_PRESETS.forEach(preset => {
      const count = generatedImages.filter(img =>
        img.prompt.toLowerCase().includes(preset.name.toLowerCase()) ||
        img.prompt.toLowerCase().includes(preset.prompt.split(',')[0].toLowerCase())
      ).length;
      if (count > 0) styleDistribution[preset.name] = count;
    });

    // Daily generations (last 7 days)
    const dailyGenerations: { date: string; count: number }[] = [];
    const now = new Date();
    for (let i = 6; i >= 0; i--) {
      const date = new Date(now);
      date.setDate(date.getDate() - i);
      const dateStr = date.toLocaleDateString('en-US', { weekday: 'short' });
      const count = generatedImages.filter(img => {
        const imgDate = new Date(img.createdAt);
        return imgDate.toDateString() === date.toDateString();
      }).length;
      dailyGenerations.push({ date: dateStr, count });
    }

    return {
      totalGenerated: generatedImages.length,
      totalFavorites: favorites,
      avgGenerationTime: avgTime,
      totalTime,
      styleDistribution,
      dailyGenerations,
    };
  }, [generatedImages]);

  // Filtered images
  const filteredImages = useMemo(() => {
    let images = [...generatedImages];

    // Filter by tab
    if (activeTab === 'favorites') {
      images = images.filter(img => img.starred);
    } else if (activeTab === 'recent') {
      const dayAgo = new Date();
      dayAgo.setDate(dayAgo.getDate() - 1);
      images = images.filter(img => new Date(img.createdAt) > dayAgo);
    }

    // Filter by search
    if (searchQuery) {
      images = images.filter(img =>
        img.prompt.toLowerCase().includes(searchQuery.toLowerCase())
      );
    }

    // Sort by date (newest first)
    return images.sort((a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime());
  }, [generatedImages, activeTab, searchQuery]);

  // Chart data
  const styleChartData = useMemo(() => {
    return Object.entries(stats.styleDistribution).map(([name, value], i) => ({
      name,
      value,
      fill: CHART_COLORS[i % CHART_COLORS.length],
    }));
  }, [stats.styleDistribution]);

  // Generate mutation
  const generateMutation = useMutation({
    mutationFn: async () => {
      const startTime = Date.now();
      const response = await generateImage({
        model: selectedModel,
        prompt,
        negative_prompt: settings.negativePrompt,
        width: settings.width,
        height: settings.height,
        steps: settings.steps,
        cfg_scale: settings.cfgScale,
        seed: settings.seed === -1 ? Math.floor(Math.random() * 2147483647) : settings.seed,
        sampler: settings.sampler,
        batch_size: settings.batchSize,
      });
      return {
        ...response,
        generationTime: Date.now() - startTime,
      };
    },
    onSuccess: (data) => {
      const model = diffusionModels.find((m: any) => m.id === selectedModel);
      const newImages: GeneratedImage[] = (data.images || []).map((img: string, i: number) => ({
        id: crypto.randomUUID(),
        url: img,
        prompt,
        negativePrompt: settings.negativePrompt,
        width: settings.width,
        height: settings.height,
        steps: settings.steps,
        cfgScale: settings.cfgScale,
        seed: data.seed + i,
        sampler: settings.sampler,
        modelId: selectedModel,
        modelName: model?.name || selectedModel,
        createdAt: new Date(),
        generationTime: data.generationTime,
        starred: false,
      }));
      setGeneratedImages(prev => [...newImages, ...prev]);
      setSelectedImage(newImages[0]);
      // Clear progress interval and complete
      if (progressIntervalRef.current) {
        clearInterval(progressIntervalRef.current);
        progressIntervalRef.current = null;
      }
      setGenerationProgress(100);
      setTimeout(() => {
        setIsGenerating(false);
        setGenerationProgress(0);
      }, 300);
    },
    onError: (error: any) => {
      // Clear progress interval
      if (progressIntervalRef.current) {
        clearInterval(progressIntervalRef.current);
        progressIntervalRef.current = null;
      }
      setIsGenerating(false);
      setGenerationProgress(0);
      const errorMessage = error?.response?.data?.detail || error?.message || 'Image generation failed';
      setGenerationError(errorMessage);
      console.error('[Image Generation Error]', error);
    },
  });

  // Handle generate
  const handleGenerate = () => {
    if (!prompt.trim() || !selectedModel) return;
    setIsGenerating(true);
    setGenerationProgress(0);
    setGenerationError(null);

    // Clear any existing interval
    if (progressIntervalRef.current) {
      clearInterval(progressIntervalRef.current);
    }

    // Simulate progress (will be cleared when mutation completes)
    progressIntervalRef.current = setInterval(() => {
      setGenerationProgress(prev => {
        if (prev >= 95) return prev; // Cap at 95% until complete
        return prev + Math.random() * 10;
      });
    }, 200);

    generateMutation.mutate();
  };

  // Apply style preset
  const applyStylePreset = (preset: typeof STYLE_PRESETS[0]) => {
    setPrompt(prev => prev ? `${prev}, ${preset.prompt}` : preset.prompt);
  };

  // Download image
  const downloadImage = (image: GeneratedImage) => {
    const link = document.createElement('a');
    link.href = image.url;
    link.download = `snapllm-${image.id}.png`;
    link.click();
  };

  // Download selected images
  const downloadSelected = () => {
    selectedImages.forEach(id => {
      const img = generatedImages.find(i => i.id === id);
      if (img) downloadImage(img);
    });
    setSelectedImages(new Set());
    setIsSelectMode(false);
  };

  // Delete image
  const deleteImage = (id: string) => {
    setGeneratedImages(prev => prev.filter(img => img.id !== id));
    if (selectedImage?.id === id) setSelectedImage(null);
  };

  // Delete selected images
  const deleteSelected = () => {
    setGeneratedImages(prev => prev.filter(img => !selectedImages.has(img.id)));
    setSelectedImages(new Set());
    setIsSelectMode(false);
  };

  // Toggle favorite
  const toggleFavorite = (id: string) => {
    setGeneratedImages(prev =>
      prev.map(img =>
        img.id === id ? { ...img, starred: !img.starred } : img
      )
    );
    if (selectedImage?.id === id) {
      setSelectedImage(prev => prev ? { ...prev, starred: !prev.starred } : null);
    }
  };

  // Save prompt
  const savePrompt = () => {
    if (!prompt.trim()) return;
    const newPrompt: SavedPrompt = {
      id: crypto.randomUUID(),
      text: prompt,
      negativePrompt: settings.negativePrompt,
      settings: { ...settings },
      usageCount: 0,
      createdAt: new Date(),
    };
    setSavedPrompts(prev => [newPrompt, ...prev]);
  };

  // Use saved prompt
  const useSavedPrompt = (saved: SavedPrompt) => {
    setPrompt(saved.text);
    if (saved.negativePrompt) {
      setSettings(s => ({ ...s, negativePrompt: saved.negativePrompt! }));
    }
    if (saved.settings) {
      setSettings(s => ({ ...s, ...saved.settings }));
    }
    setSavedPrompts(prev =>
      prev.map(p => p.id === saved.id ? { ...p, usageCount: p.usageCount + 1 } : p)
    );
    setShowPromptLibrary(false);
  };

  // Delete saved prompt
  const deleteSavedPrompt = (id: string) => {
    setSavedPrompts(prev => prev.filter(p => p.id !== id));
  };

  // Set aspect ratio
  const setAspectRatio = (ratio: typeof ASPECT_RATIOS[0]) => {
    setSettings(s => ({ ...s, width: ratio.width, height: ratio.height }));
  };

  // Copy settings from image
  const copyImageSettings = (image: GeneratedImage) => {
    setSettings({
      width: image.width,
      height: image.height,
      steps: image.steps,
      cfgScale: image.cfgScale,
      seed: image.seed,
      sampler: image.sampler,
      negativePrompt: image.negativePrompt || '',
      batchSize: 1,
    });
    setPrompt(image.prompt);
  };

  // Toggle image selection
  const toggleImageSelection = (id: string) => {
    setSelectedImages(prev => {
      const newSet = new Set(prev);
      if (newSet.has(id)) {
        newSet.delete(id);
      } else {
        newSet.add(id);
      }
      return newSet;
    });
  };

  // Export gallery
  const exportGallery = () => {
    const data = {
      images: generatedImages,
      prompts: savedPrompts,
      exportedAt: new Date().toISOString(),
    };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `snapllm-gallery-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-4">
          <div className="w-12 h-12 rounded-xl bg-gradient-to-br from-pink-500 to-rose-600 flex items-center justify-center">
            <ImageIcon className="w-6 h-6 text-white" />
          </div>
          <div>
            <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
              Image Studio
            </h1>
            <p className="text-surface-500">
              Generate and manage AI images with Stable Diffusion
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
              {diffusionModels.map((model: any) => (
                <option key={model.id} value={model.id}>
                  {model.name}
                </option>
              ))}
            </select>
            <Cpu className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
            <ChevronDown className="absolute right-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
          </div>

          <Button variant="secondary" onClick={() => setShowAnalytics(!showAnalytics)}>
            <BarChart3 className={clsx('w-4 h-4', showAnalytics && 'text-brand-500')} />
            Analytics
          </Button>
          <Button variant="secondary" onClick={exportGallery}>
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
              <ImagePlus className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {stats.totalGenerated}
              </p>
              <p className="text-sm text-surface-500">Images Generated</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Star className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {stats.totalFavorites}
              </p>
              <p className="text-sm text-surface-500">Favorites</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <Clock className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {(stats.avgGenerationTime / 1000).toFixed(1)}s
              </p>
              <p className="text-sm text-surface-500">Avg Gen Time</p>
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
                {savedPrompts.length}
              </p>
              <p className="text-sm text-surface-500">Saved Prompts</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Analytics Section */}
      <AnimatePresence>
        {showAnalytics && (
          <motion.div
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: 'auto' }}
            exit={{ opacity: 0, height: 0 }}
            className="grid grid-cols-2 gap-6"
          >
            {/* Daily Generations Chart */}
            <Card className="p-6">
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <TrendingUp className="w-5 h-5 text-brand-500" />
                Generation Trend (7 Days)
              </h3>
              <div className="h-48">
                <ResponsiveContainer width="100%" height="100%">
                  <BarChart data={stats.dailyGenerations}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
                    <XAxis dataKey="date" tick={{ fontSize: 12 }} />
                    <YAxis tick={{ fontSize: 12 }} />
                    <Tooltip content={<CustomTooltip />} />
                    <Bar dataKey="count" name="Images" fill="#ec4899" radius={[4, 4, 0, 0]} />
                  </BarChart>
                </ResponsiveContainer>
              </div>
            </Card>

            {/* Style Distribution Chart */}
            <Card className="p-6">
              <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-4 flex items-center gap-2">
                <PieChartIcon className="w-5 h-5 text-ai-purple" />
                Style Distribution
              </h3>
              <div className="h-48">
                {styleChartData.length > 0 ? (
                  <ResponsiveContainer width="100%" height="100%">
                    <PieChart>
                      <Pie
                        data={styleChartData}
                        cx="50%"
                        cy="50%"
                        outerRadius={60}
                        dataKey="value"
                        label={({ name, percent }) => `${name} (${(percent * 100).toFixed(0)}%)`}
                        labelLine={false}
                      >
                        {styleChartData.map((entry, index) => (
                          <Cell key={`cell-${index}`} fill={entry.fill} />
                        ))}
                      </Pie>
                      <Tooltip />
                    </PieChart>
                  </ResponsiveContainer>
                ) : (
                  <div className="h-full flex items-center justify-center text-surface-500">
                    <p>No style data yet</p>
                  </div>
                )}
              </div>
            </Card>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Main Content */}
      <div className="grid grid-cols-[1fr_320px] gap-6">
        {/* Left Side - Gallery & Generation */}
        <div className="space-y-6">
          {/* Prompt Input */}
          <Card className="p-4">
            {/* Generation Progress */}
            {isGenerating && (
              <div className="mb-4">
                <div className="flex items-center justify-between text-sm mb-2">
                  <span className="text-surface-600 dark:text-surface-400">Generating...</span>
                  <span className="text-surface-500">{Math.round(generationProgress)}%</span>
                </div>
                <Progress value={generationProgress} variant="default" />
              </div>
            )}

            {/* Generation Error */}
            {generationError && (
              <div className="mb-4 p-3 rounded-lg bg-red-50 dark:bg-red-900/20 border border-red-200 dark:border-red-800">
                <div className="flex items-start gap-2">
                  <AlertTriangle className="w-5 h-5 text-red-500 flex-shrink-0 mt-0.5" />
                  <div className="flex-1">
                    <p className="text-sm font-medium text-red-700 dark:text-red-400">Generation Failed</p>
                    <p className="text-sm text-red-600 dark:text-red-300 mt-1">{generationError}</p>
                  </div>
                  <button
                    onClick={() => setGenerationError(null)}
                    className="text-red-400 hover:text-red-600"
                  >
                    <span className="sr-only">Dismiss</span>
                    ×
                  </button>
                </div>
              </div>
            )}

            <div className="flex gap-3 mb-4">
              <div className="flex-1 relative">
                <textarea
                  value={prompt}
                  onChange={(e) => setPrompt(e.target.value)}
                  placeholder="Describe the image you want to generate..."
                  rows={3}
                  className="w-full resize-none rounded-xl border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-4 py-3 text-[15px] placeholder:text-surface-400 focus:outline-none focus:ring-2 focus:ring-brand-500 focus:border-transparent"
                />
              </div>
              <div className="flex flex-col gap-2">
                <Button
                  variant="gradient"
                  size="lg"
                  onClick={handleGenerate}
                  disabled={!prompt.trim() || !selectedModel || isGenerating}
                  className="flex-1 min-w-[140px]"
                >
                  {isGenerating ? (
                    <Loader2 className="w-5 h-5 animate-spin" />
                  ) : (
                    <>
                      <Sparkles className="w-5 h-5" />
                      Generate
                    </>
                  )}
                </Button>
                <div className="flex gap-1">
                  <IconButton
                    icon={<Save className="w-4 h-4" />}
                    label="Save Prompt"
                    onClick={savePrompt}
                  />
                  <IconButton
                    icon={<BookMarked className="w-4 h-4" />}
                    label="Prompt Library"
                    onClick={() => setShowPromptLibrary(true)}
                  />
                </div>
              </div>
            </div>

            {/* Style Presets */}
            <div className="flex flex-wrap gap-2">
              {STYLE_PRESETS.map((preset) => (
                <button
                  key={preset.name}
                  onClick={() => applyStylePreset(preset)}
                  className="px-3 py-1.5 rounded-lg text-xs font-medium transition-colors"
                  style={{
                    backgroundColor: `${preset.color}20`,
                    color: preset.color,
                  }}
                >
                  {preset.name}
                </button>
              ))}
            </div>
          </Card>

          {/* Gallery Controls */}
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              {(['gallery', 'favorites', 'recent'] as const).map(tab => (
                <button
                  key={tab}
                  onClick={() => setActiveTab(tab)}
                  className={clsx(
                    'px-4 py-2 rounded-lg text-sm font-medium transition-colors',
                    activeTab === tab
                      ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300'
                      : 'text-surface-600 dark:text-surface-400 hover:bg-surface-100 dark:hover:bg-surface-800'
                  )}
                >
                  {tab === 'gallery' && <FileImage className="w-4 h-4 mr-1.5 inline" />}
                  {tab === 'favorites' && <Star className="w-4 h-4 mr-1.5 inline" />}
                  {tab === 'recent' && <Clock className="w-4 h-4 mr-1.5 inline" />}
                  {tab.charAt(0).toUpperCase() + tab.slice(1)}
                  <Badge variant="default" className="ml-2">
                    {tab === 'gallery' ? generatedImages.length :
                     tab === 'favorites' ? stats.totalFavorites :
                     filteredImages.length}
                  </Badge>
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
                  placeholder="Search prompts..."
                  className="pl-9 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-sm w-48 focus:outline-none focus:ring-2 focus:ring-brand-500"
                />
              </div>

              {/* Select Mode */}
              <Button
                variant={isSelectMode ? 'primary' : 'secondary'}
                size="sm"
                onClick={() => {
                  setIsSelectMode(!isSelectMode);
                  setSelectedImages(new Set());
                }}
              >
                <CheckSquare className="w-4 h-4" />
                Select
              </Button>

              {/* View Mode */}
              <div className="flex items-center border border-surface-200 dark:border-surface-700 rounded-lg overflow-hidden">
                <button
                  onClick={() => setViewMode('grid')}
                  className={clsx(
                    'p-2',
                    viewMode === 'grid' ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-600' : 'text-surface-500'
                  )}
                >
                  <LayoutGrid className="w-4 h-4" />
                </button>
                <button
                  onClick={() => setViewMode('list')}
                  className={clsx(
                    'p-2',
                    viewMode === 'list' ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-600' : 'text-surface-500'
                  )}
                >
                  <List className="w-4 h-4" />
                </button>
              </div>
            </div>
          </div>

          {/* Selection Actions */}
          {isSelectMode && selectedImages.size > 0 && (
            <div className="flex items-center justify-between p-3 bg-brand-50 dark:bg-brand-900/20 rounded-lg">
              <span className="text-sm font-medium text-brand-700 dark:text-brand-300">
                {selectedImages.size} selected
              </span>
              <div className="flex items-center gap-2">
                <Button variant="secondary" size="sm" onClick={downloadSelected}>
                  <Download className="w-4 h-4" />
                  Download
                </Button>
                <Button variant="danger" size="sm" onClick={deleteSelected}>
                  <Trash2 className="w-4 h-4" />
                  Delete
                </Button>
              </div>
            </div>
          )}

          {/* Gallery Grid */}
          {filteredImages.length > 0 ? (
            <div className={clsx(
              viewMode === 'grid'
                ? 'grid grid-cols-3 gap-4'
                : 'space-y-3'
            )}>
              {filteredImages.map((img) => (
                <motion.div
                  key={img.id}
                  initial={{ opacity: 0, scale: 0.95 }}
                  animate={{ opacity: 1, scale: 1 }}
                  className={clsx(
                    'relative group rounded-xl overflow-hidden border-2 transition-all cursor-pointer',
                    selectedImages.has(img.id)
                      ? 'border-brand-500 ring-2 ring-brand-500/30'
                      : selectedImage?.id === img.id
                        ? 'border-brand-300 dark:border-brand-700'
                        : 'border-transparent hover:border-surface-300 dark:hover:border-surface-600'
                  )}
                  onClick={() => {
                    if (isSelectMode) {
                      toggleImageSelection(img.id);
                    } else {
                      setSelectedImage(img);
                      setShowImageModal(true);
                    }
                  }}
                >
                  {viewMode === 'grid' ? (
                    <>
                      <img
                        src={img.url}
                        alt={img.prompt}
                        className="w-full aspect-square object-cover"
                      />
                      {/* Overlay */}
                      <div className="absolute inset-0 bg-gradient-to-t from-black/60 via-transparent opacity-0 group-hover:opacity-100 transition-opacity">
                        <div className="absolute bottom-0 left-0 right-0 p-3">
                          <p className="text-white text-xs line-clamp-2">{img.prompt}</p>
                        </div>
                      </div>
                      {/* Actions */}
                      <div className="absolute top-2 right-2 flex gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
                        <button
                          onClick={(e) => { e.stopPropagation(); toggleFavorite(img.id); }}
                          className="p-1.5 rounded-lg bg-white/90 dark:bg-surface-900/90"
                        >
                          <Star className={clsx('w-4 h-4', img.starred ? 'fill-warning-500 text-warning-500' : 'text-surface-600')} />
                        </button>
                        <button
                          onClick={(e) => { e.stopPropagation(); downloadImage(img); }}
                          className="p-1.5 rounded-lg bg-white/90 dark:bg-surface-900/90"
                        >
                          <Download className="w-4 h-4 text-surface-600" />
                        </button>
                      </div>
                      {/* Selection Checkbox */}
                      {isSelectMode && (
                        <div className="absolute top-2 left-2">
                          <div className={clsx(
                            'w-5 h-5 rounded border-2 flex items-center justify-center',
                            selectedImages.has(img.id)
                              ? 'bg-brand-500 border-brand-500'
                              : 'bg-white/90 border-surface-300'
                          )}>
                            {selectedImages.has(img.id) && <Check className="w-3 h-3 text-white" />}
                          </div>
                        </div>
                      )}
                    </>
                  ) : (
                    <div className="flex items-center gap-4 p-3">
                      <img
                        src={img.url}
                        alt={img.prompt}
                        className="w-20 h-20 rounded-lg object-cover flex-shrink-0"
                      />
                      <div className="flex-1 min-w-0">
                        <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-2">{img.prompt}</p>
                        <div className="flex items-center gap-3 mt-2 text-xs text-surface-500">
                          <span>{img.width}×{img.height}</span>
                          <span>{img.steps} steps</span>
                          <span>CFG {img.cfgScale}</span>
                          <span>{(img.generationTime / 1000).toFixed(1)}s</span>
                        </div>
                      </div>
                      <div className="flex items-center gap-2">
                        <IconButton icon={<Star className={clsx('w-4 h-4', img.starred && 'fill-warning-500 text-warning-500')} />} label="Favorite" onClick={() => toggleFavorite(img.id)} />
                        <IconButton icon={<Download className="w-4 h-4" />} label="Download" onClick={() => downloadImage(img)} />
                        <IconButton icon={<Copy className="w-4 h-4" />} label="Copy Settings" onClick={() => copyImageSettings(img)} />
                        <IconButton icon={<Trash2 className="w-4 h-4 text-error-500" />} label="Delete" onClick={() => deleteImage(img.id)} />
                      </div>
                    </div>
                  )}
                </motion.div>
              ))}
            </div>
          ) : (
            <Card className="p-12 text-center">
              <div className="w-20 h-20 rounded-2xl bg-gradient-to-br from-pink-500/20 to-rose-600/20 flex items-center justify-center mx-auto mb-6">
                <Sparkles className="w-10 h-10 text-pink-500" />
              </div>
              <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
                {activeTab === 'favorites' ? 'No Favorites Yet' :
                 activeTab === 'recent' ? 'No Recent Images' :
                 'Create Something Amazing'}
              </h2>
              <p className="text-surface-500 max-w-md mx-auto">
                {activeTab === 'favorites'
                  ? 'Star your favorite images to see them here'
                  : activeTab === 'recent'
                    ? 'Images generated in the last 24 hours will appear here'
                    : 'Enter a prompt and let AI generate stunning images for you'}
              </p>
            </Card>
          )}
        </div>

        {/* Right Side - Settings */}
        <div className="space-y-4">
          <Card className="p-4">
            <h3 className="font-semibold text-surface-900 dark:text-white flex items-center gap-2 mb-4">
              <SlidersHorizontal className="w-4 h-4" />
              Generation Settings
            </h3>

            {/* Aspect Ratio */}
            <div className="mb-4">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                Aspect Ratio
              </label>
              <div className="flex gap-2">
                {ASPECT_RATIOS.map((ratio) => (
                  <button
                    key={ratio.label}
                    onClick={() => setAspectRatio(ratio)}
                    className={clsx(
                      'flex-1 py-2 rounded-lg text-sm font-medium transition-all',
                      settings.width === ratio.width && settings.height === ratio.height
                        ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300 ring-1 ring-brand-500'
                        : 'bg-surface-100 dark:bg-surface-800 text-surface-600 dark:text-surface-400 hover:bg-surface-200'
                    )}
                  >
                    {ratio.label}
                  </button>
                ))}
              </div>
              <p className="text-xs text-surface-500 mt-1">{settings.width} × {settings.height} px</p>
            </div>

            {/* Steps */}
            <div className="mb-4">
              <div className="flex justify-between mb-1">
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300">Steps</label>
                <span className="text-sm text-surface-500">{settings.steps}</span>
              </div>
              <input
                type="range"
                min="10"
                max="100"
                value={settings.steps}
                onChange={(e) => setSettings(s => ({ ...s, steps: parseInt(e.target.value) }))}
                className="w-full accent-brand-500"
              />
            </div>

            {/* CFG Scale */}
            <div className="mb-4">
              <div className="flex justify-between mb-1">
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300">CFG Scale</label>
                <span className="text-sm text-surface-500">{settings.cfgScale}</span>
              </div>
              <input
                type="range"
                min="1"
                max="20"
                step="0.5"
                value={settings.cfgScale}
                onChange={(e) => setSettings(s => ({ ...s, cfgScale: parseFloat(e.target.value) }))}
                className="w-full accent-brand-500"
              />
            </div>

            {/* Sampler */}
            <div className="mb-4">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1 block">Sampler</label>
              <select
                value={settings.sampler}
                onChange={(e) => setSettings(s => ({ ...s, sampler: e.target.value }))}
                className="w-full rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-3 py-2 text-sm focus:outline-none focus:ring-2 focus:ring-brand-500"
              >
                {SAMPLERS.map((sampler) => (
                  <option key={sampler} value={sampler}>{sampler}</option>
                ))}
              </select>
            </div>

            {/* Seed */}
            <div className="mb-4">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1 block">Seed</label>
              <div className="flex gap-2">
                <input
                  type="number"
                  value={settings.seed}
                  onChange={(e) => setSettings(s => ({ ...s, seed: parseInt(e.target.value) || -1 }))}
                  className="flex-1 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-3 py-2 text-sm"
                  placeholder="-1 for random"
                />
                <Button variant="secondary" size="sm" onClick={() => setSettings(s => ({ ...s, seed: -1 }))}>
                  <RefreshCw className="w-4 h-4" />
                </Button>
              </div>
            </div>

            {/* Negative Prompt */}
            <div className="mb-4">
              <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-1 block">Negative Prompt</label>
              <textarea
                value={settings.negativePrompt}
                onChange={(e) => setSettings(s => ({ ...s, negativePrompt: e.target.value }))}
                rows={2}
                className="w-full rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800 px-3 py-2 text-sm resize-none"
                placeholder="What to avoid..."
              />
            </div>

            {/* Batch Size */}
            <div>
              <div className="flex justify-between mb-1">
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300">Batch Size</label>
                <span className="text-sm text-surface-500">{settings.batchSize}</span>
              </div>
              <div className="flex items-center gap-2">
                <Button variant="secondary" size="sm" onClick={() => setSettings(s => ({ ...s, batchSize: Math.max(1, s.batchSize - 1) }))} disabled={settings.batchSize <= 1}>
                  <Minus className="w-4 h-4" />
                </Button>
                <div className="flex-1 text-center text-lg font-semibold">{settings.batchSize}</div>
                <Button variant="secondary" size="sm" onClick={() => setSettings(s => ({ ...s, batchSize: Math.min(4, s.batchSize + 1) }))} disabled={settings.batchSize >= 4}>
                  <Plus className="w-4 h-4" />
                </Button>
              </div>
            </div>
          </Card>
        </div>
      </div>

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
                  <p className="text-sm text-surface-700 dark:text-surface-300 line-clamp-2">{saved.text}</p>
                  <div className="flex items-center gap-2 mt-1 text-xs text-surface-500">
                    <span>Used {saved.usageCount} times</span>
                    <span>•</span>
                    <span>{new Date(saved.createdAt).toLocaleDateString()}</span>
                  </div>
                </div>
                <div className="flex items-center gap-1">
                  <Button variant="primary" size="sm" onClick={() => useSavedPrompt(saved)}>
                    Use
                  </Button>
                  <IconButton icon={<Trash2 className="w-4 h-4 text-error-500" />} label="Delete" onClick={() => deleteSavedPrompt(saved.id)} />
                </div>
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

      {/* Image Detail Modal */}
      <Modal isOpen={showImageModal && !!selectedImage} onClose={() => setShowImageModal(false)} title="Image Details" size="xl">
        {selectedImage && (
          <div className="flex gap-6">
            <div className="flex-1">
              <img
                src={selectedImage.url}
                alt={selectedImage.prompt}
                className="w-full rounded-xl"
              />
            </div>
            <div className="w-80 space-y-4">
              <div>
                <h4 className="text-sm font-semibold text-surface-700 dark:text-surface-300 mb-1">Prompt</h4>
                <p className="text-sm text-surface-600 dark:text-surface-400">{selectedImage.prompt}</p>
              </div>
              {selectedImage.negativePrompt && (
                <div>
                  <h4 className="text-sm font-semibold text-surface-700 dark:text-surface-300 mb-1">Negative Prompt</h4>
                  <p className="text-sm text-surface-600 dark:text-surface-400">{selectedImage.negativePrompt}</p>
                </div>
              )}
              <div className="grid grid-cols-2 gap-3">
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <p className="text-xs text-surface-500">Size</p>
                  <p className="font-medium">{selectedImage.width}×{selectedImage.height}</p>
                </div>
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <p className="text-xs text-surface-500">Steps</p>
                  <p className="font-medium">{selectedImage.steps}</p>
                </div>
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <p className="text-xs text-surface-500">CFG Scale</p>
                  <p className="font-medium">{selectedImage.cfgScale}</p>
                </div>
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800">
                  <p className="text-xs text-surface-500">Seed</p>
                  <p className="font-medium font-mono">{selectedImage.seed}</p>
                </div>
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800 col-span-2">
                  <p className="text-xs text-surface-500">Sampler</p>
                  <p className="font-medium">{selectedImage.sampler}</p>
                </div>
                <div className="p-2 rounded-lg bg-surface-50 dark:bg-surface-800 col-span-2">
                  <p className="text-xs text-surface-500">Generation Time</p>
                  <p className="font-medium">{(selectedImage.generationTime / 1000).toFixed(1)}s</p>
                </div>
              </div>
              <div className="flex gap-2">
                <Button variant="secondary" className="flex-1" onClick={() => copyImageSettings(selectedImage)}>
                  <Copy className="w-4 h-4" />
                  Copy Settings
                </Button>
                <Button variant="primary" className="flex-1" onClick={() => downloadImage(selectedImage)}>
                  <Download className="w-4 h-4" />
                  Download
                </Button>
              </div>
            </div>
          </div>
        )}
      </Modal>
    </div>
  );
}
