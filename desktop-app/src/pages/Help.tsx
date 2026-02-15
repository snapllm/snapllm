// ============================================================================
// SnapLLM - Help & Documentation
// Guides, API Reference, and Support
// ============================================================================

import React, { useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { clsx } from 'clsx';
import {
  HelpCircle,
  Code2,
  MessageSquare,
  ExternalLink,
  Search,
  ChevronRight,
  Zap,
  Cpu,
  Database,
  Image as ImageIcon,
  Eye,
  Bot,
  Workflow,
  Github,
  BookOpen,
  Lightbulb,
  Rocket,
} from 'lucide-react';
import { Card } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface HelpArticle {
  id: string;
  title: string;
  description: string;
  category: string;
  icon: React.ComponentType<{ className?: string }>;
  content: string[];
}

interface QuickLink {
  title: string;
  description: string;
  icon: React.ComponentType<{ className?: string }>;
  url: string;
}

// ============================================================================
// Content
// ============================================================================

const GETTING_STARTED: HelpArticle[] = [
  {
    id: '1',
    title: 'Quick Start Guide',
    description: 'Get up and running in a few minutes.',
    category: 'basics',
    icon: Rocket,
    content: [
      'Start the backend with Start_Server.bat and confirm /health responds.',
      'Launch the desktop app from src-tauri/target/release/SnapLLM.exe.',
      'Load a model from the Models page, then verify it appears in Chat.',
    ],
  },
  {
    id: '2',
    title: 'Loading Your First Model',
    description: 'Load and switch models locally.',
    category: 'models',
    icon: Cpu,
    content: [
      'Place GGUF files in your models folder (default: C:/Models or D:/Models).',
      'Use Models > Load Model to select a file and set optional paths.',
      'Switch the active model and validate via /api/v1/models.',
    ],
  },
  {
    id: '3',
    title: 'Chat Interface Basics',
    description: 'Use the chat UI and streaming responses.',
    category: 'chat',
    icon: MessageSquare,
    content: [
      'Select a loaded model to enable the input box.',
      'Adjust temperature, top_p, and max tokens in settings.',
      'Use streaming to see tokens live and measure throughput.',
    ],
  },
  {
    id: '4',
    title: 'Understanding vPID',
    description: 'Learn how tiered caching accelerates switching.',
    category: 'architecture',
    icon: Zap,
    content: [
      'vPID keeps models in HOT/WARM/COLD tiers to reduce reload time.',
      'Use /api/v1/models/cache/stats to see cache performance.',
      'Clear caches only when reclaiming RAM or troubleshooting.',
    ],
  },
];

const FEATURE_GUIDES: HelpArticle[] = [
  {
    id: '1',
    title: 'Image Generation',
    description: 'Generate images with Stable Diffusion.',
    icon: ImageIcon,
    category: 'images',
    content: [
      'Load a diffusion model via Models > Load Model (type: diffusion).',
      'Use Images page or POST /api/v1/diffusion/generate.',
      'Generated files are saved under the workspace images folder.',
    ],
  },
  {
    id: '2',
    title: 'Vision & Multimodal',
    description: 'Analyze images with vision models.',
    icon: Eye,
    category: 'vision',
    content: [
      'Load a vision model with the required mmproj path.',
      'Send base64 images to /api/v1/vision/generate.',
      'Adjust temperature, top_p, top_k, and repeat_penalty for control.',
    ],
  },
  {
    id: '3',
    title: 'RAG & Knowledge Bases',
    description: 'Build retrieval-augmented workflows.',
    icon: Database,
    category: 'rag',
    content: [
      'Ingest documents using /api/v1/contexts/ingest to pre-compute KV.',
      'Query contexts via /api/v1/contexts/{context_id}/query.',
      'Review tier usage with /api/v1/contexts/stats.',
    ],
  },
  {
    id: '4',
    title: 'AI Agents',
    description: 'Tool-call style prompts with local models.',
    icon: Bot,
    category: 'agents',
    content: [
      'Use /v1/messages with tool definitions for structured calls.',
      'Return tool results back into the conversation loop.',
      'Keep prompts local-only for full privacy.',
    ],
  },
  {
    id: '5',
    title: 'Workflow Builder',
    description: 'Compose pipelines with local endpoints.',
    icon: Workflow,
    category: 'workflows',
    content: [
      'Chain calls across chat, diffusion, and vision endpoints.',
      'Persist intermediate outputs in your workspace folders.',
      'Automate flows with simple scripts and CLI commands.',
    ],
  },
  {
    id: '6',
    title: 'API Integration',
    description: 'Integrate SnapLLM into your apps.',
    icon: Code2,
    category: 'api',
    content: [
      'Use /v1/chat/completions for OpenAI-style chat.',
      'Use /v1/messages for Anthropic-style workflows.',
      'List and switch models via /api/v1/models endpoints.',
    ],
  },
];

const QUICK_LINKS: QuickLink[] = [
  { title: 'GitHub Repository', description: 'Source code and issues', icon: Github, url: 'https://github.com/snapllm/snapllm' },
];

const FAQ = [
  {
    question: 'What models does SnapLLM support?',
    answer: 'SnapLLM supports GGUF, GGML, and safetensors formats. This includes LLaMA, Mistral, Gemma, Qwen, and many more architectures.',
  },
  {
    question: 'How does vPID model switching work?',
    answer: 'vPID (Virtual Processing-In-Disk) keeps models in a 3-tier cache (HOT/WARM/COLD) allowing instant switching without reloading. This achieves sub-millisecond switch times.',
  },
  {
    question: 'Can I run SnapLLM on CPU only?',
    answer: 'Yes! SnapLLM automatically detects your hardware and uses CPU if no GPU is available. GPU acceleration with CUDA provides better performance.',
  },
  {
    question: 'How do I download models from HuggingFace?',
    answer: 'Go to the Model Hub and click "Download from HuggingFace". Enter the model ID (e.g., TheBloke/Llama-2-7B-GGUF) and select your preferred quantization.',
  },
  {
    question: 'What is the difference between quantization levels?',
    answer: 'Q4_K_M offers good balance of speed and quality. Q8_0 provides best quality but uses more memory. Q4_0 is fastest but lower quality.',
  },
];

// ============================================================================
// Main Component
// ============================================================================

export default function Help() {
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedFaq, setExpandedFaq] = useState<number | null>(null);
  const normalizedQuery = searchQuery.trim().toLowerCase();

  const filteredGettingStarted = useMemo(() => {
    if (!normalizedQuery) return GETTING_STARTED;
    return GETTING_STARTED.filter((article) =>
      article.title.toLowerCase().includes(normalizedQuery)
      || article.description.toLowerCase().includes(normalizedQuery)
      || article.content.some((line) => line.toLowerCase().includes(normalizedQuery))
    );
  }, [normalizedQuery]);

  const filteredFeatureGuides = useMemo(() => {
    if (!normalizedQuery) return FEATURE_GUIDES;
    return FEATURE_GUIDES.filter((article) =>
      article.title.toLowerCase().includes(normalizedQuery)
      || article.description.toLowerCase().includes(normalizedQuery)
      || article.content.some((line) => line.toLowerCase().includes(normalizedQuery))
    );
  }, [normalizedQuery]);

  const filteredFaq = useMemo(() => {
    if (!normalizedQuery) return FAQ;
    return FAQ.filter((item) =>
      item.question.toLowerCase().includes(normalizedQuery)
      || item.answer.toLowerCase().includes(normalizedQuery)
    );
  }, [normalizedQuery]);

  return (
    <div className="space-y-6 max-w-5xl mx-auto">
      {/* Header */}
      <div className="text-center">
        <div className="w-16 h-16 rounded-2xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center mx-auto mb-4">
          <HelpCircle className="w-8 h-8 text-white" />
        </div>
        <h1 className="text-3xl font-bold text-surface-900 dark:text-white mb-2">
          Help & Documentation
        </h1>
        <p className="text-surface-500 max-w-lg mx-auto">
          Everything you need to know about SnapLLM. Find guides, tutorials,
          and answers to common questions.
        </p>
      </div>

      {/* Search */}
      <div className="relative max-w-xl mx-auto">
        <Search className="absolute left-4 top-1/2 -translate-y-1/2 w-5 h-5 text-surface-400" />
        <input
          type="text"
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          placeholder="Search documentation..."
          className="w-full pl-12 pr-4 py-3 rounded-xl border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 text-lg focus:outline-none focus:ring-2 focus:ring-brand-500"
        />
      </div>

      {/* Quick Links */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 max-w-2xl mx-auto">
        {QUICK_LINKS.map((link, i) => (
          <motion.a
            key={link.title}
            href={link.url}
            initial={{ opacity: 0, y: 20 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ delay: i * 0.1 }}
            className="card-hover p-4 flex items-center gap-3"
          >
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <link.icon className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div className="flex-1 min-w-0">
              <p className="font-medium text-surface-900 dark:text-white">{link.title}</p>
              <p className="text-sm text-surface-500 truncate">{link.description}</p>
            </div>
            <ExternalLink className="w-4 h-4 text-surface-400" />
          </motion.a>
        ))}
      </div>

      {/* Getting Started */}
      <Card className="p-6">
        <div className="flex items-center gap-3 mb-4">
          <Rocket className="w-5 h-5 text-brand-500" />
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
            Getting Started
          </h2>
        </div>
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {filteredGettingStarted.map((article) => (
            <div
              key={article.id}
              className="p-4 rounded-xl border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
            >
              <div className="flex items-start gap-3 mb-3">
                <div className="w-10 h-10 rounded-lg bg-surface-100 dark:bg-surface-700 flex items-center justify-center">
                  <article.icon className="w-5 h-5 text-surface-500" />
                </div>
                <div>
                  <p className="font-medium text-surface-900 dark:text-white">{article.title}</p>
                  <p className="text-sm text-surface-500">{article.description}</p>
                </div>
              </div>
              <ul className="list-disc pl-5 text-sm text-surface-600 dark:text-surface-400 space-y-1">
                {article.content.map((line) => (
                  <li key={line}>{line}</li>
                ))}
              </ul>
            </div>
          ))}
        </div>
        {filteredGettingStarted.length === 0 && (
          <p className="text-sm text-surface-500">No getting started topics match your search.</p>
        )}
      </Card>

      {/* Feature Guides */}
      <Card className="p-6">
        <div className="flex items-center gap-3 mb-4">
          <BookOpen className="w-5 h-5 text-brand-500" />
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
            Feature Guides
          </h2>
        </div>
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {filteredFeatureGuides.map((guide) => (
            <div
              key={guide.id}
              className="p-4 rounded-xl border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-900"
            >
              <div className="flex items-start gap-3 mb-3">
                <div className="w-10 h-10 rounded-lg bg-gradient-to-br from-brand-500/20 to-ai-purple/20 flex items-center justify-center">
                  <guide.icon className="w-5 h-5 text-brand-600 dark:text-brand-400" />
                </div>
                <div>
                  <p className="font-medium text-surface-900 dark:text-white">{guide.title}</p>
                  <p className="text-sm text-surface-500">{guide.description}</p>
                </div>
              </div>
              <ul className="list-disc pl-5 text-sm text-surface-600 dark:text-surface-400 space-y-1">
                {guide.content.map((line) => (
                  <li key={line}>{line}</li>
                ))}
              </ul>
            </div>
          ))}
        </div>
        {filteredFeatureGuides.length === 0 && (
          <p className="text-sm text-surface-500">No feature guides match your search.</p>
        )}
      </Card>

      {/* FAQ */}
      <Card className="p-6">
        <div className="flex items-center gap-3 mb-4">
          <Lightbulb className="w-5 h-5 text-brand-500" />
          <h2 className="text-lg font-semibold text-surface-900 dark:text-white">
            Frequently Asked Questions
          </h2>
        </div>
        <div className="space-y-2">
          {filteredFaq.map((faq, i) => (
            <div
              key={i}
              className="border border-surface-200 dark:border-surface-700 rounded-lg overflow-hidden"
            >
              <button
                onClick={() => setExpandedFaq(expandedFaq === i ? null : i)}
                className="w-full flex items-center justify-between p-4 text-left hover:bg-surface-50 dark:hover:bg-surface-800 transition-colors"
              >
                <span className="font-medium text-surface-900 dark:text-white">
                  {faq.question}
                </span>
                <ChevronRight className={clsx(
                  'w-4 h-4 text-surface-400 transition-transform',
                  expandedFaq === i && 'rotate-90'
                )} />
              </button>
              {expandedFaq === i && (
                <motion.div
                  initial={{ height: 0, opacity: 0 }}
                  animate={{ height: 'auto', opacity: 1 }}
                  className="px-4 pb-4"
                >
                  <p className="text-surface-600 dark:text-surface-400">
                    {faq.answer}
                  </p>
                </motion.div>
              )}
            </div>
          ))}
        </div>
        {filteredFaq.length === 0 && (
          <p className="text-sm text-surface-500">No FAQ entries match your search.</p>
        )}
      </Card>
    </div>
  );
}


