// ============================================================================
// SnapLLM Enterprise - Header Component
// ============================================================================

import React, { useState } from 'react';
import { useLocation } from 'react-router-dom';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Search,
  Bell,
  Moon,
  Sun,
  Monitor,
  Zap,
  ChevronDown,
  Check,
  Cpu,
  HardDrive,
  Activity,
} from 'lucide-react';
import { useAppStore, useModelStore, useMetricsStore } from '../../store';
import { IconButton, Badge } from '../ui';
import { ModelQuickSwitcher } from '../models/ModelQuickSwitcher';

// Page titles mapping
const pageTitles: Record<string, { title: string; description: string }> = {
  '/': { title: 'Dashboard', description: 'System overview and health monitoring' },
  '/chat': { title: 'Chat', description: 'Text generation with LLMs' },
  '/images': { title: 'Image Studio', description: 'Generate images with Stable Diffusion' },
  '/vision': { title: 'Vision', description: 'Multimodal image understanding' },
  '/models': { title: 'Model Hub', description: 'Load and manage AI models' },
  '/compare': { title: 'A/B Compare', description: 'Compare model outputs side by side' },
  '/switch': { title: 'Quick Switch', description: 'Ultra-fast model switching' },
  '/playground': { title: 'API Playground', description: 'Interactive API testing' },
  '/metrics': { title: 'Metrics', description: 'Performance analytics and monitoring' },
  '/settings': { title: 'Server Settings', description: 'Runtime defaults and feature flags' },
  '/about': { title: 'About', description: 'Legacy server snapshot and build highlights' },
  '/help': { title: 'Help & Docs', description: 'Documentation and support' },
};

export const Header: React.FC = () => {
  const location = useLocation();
  const { theme, setTheme, setCommandPaletteOpen, notifications } = useAppStore();
  const { models, activeModelId, setActiveModel } = useModelStore();
  const { systemMetrics, isConnected } = useMetricsStore();

  const [showModelSwitcher, setShowModelSwitcher] = useState(false);
  const [showNotifications, setShowNotifications] = useState(false);
  const [showThemeMenu, setShowThemeMenu] = useState(false);

  const pageInfo = pageTitles[location.pathname] || { title: 'SnapLLM', description: '' };
  const activeModel = models.find((m) => m.id === activeModelId);
  const loadedModels = models.filter((m) => m.status === 'ready');
  const unreadNotifications = notifications.filter((n) => !n.read).length;

  const themeOptions = [
    { value: 'light', icon: Sun, label: 'Light' },
    { value: 'dark', icon: Moon, label: 'Dark' },
    { value: 'system', icon: Monitor, label: 'System' },
  ];

  return (
    <header className="header">
      {/* Left Section - Page Title & Breadcrumb */}
      <div className="flex items-center gap-4">
        <div>
          <h1 className="text-lg font-semibold text-surface-900 dark:text-surface-100">
            {pageInfo.title}
          </h1>
          <p className="text-sm text-surface-500">{pageInfo.description}</p>
        </div>
      </div>

      {/* Center Section - Quick Model Switcher */}
      <div className="flex-1 max-w-xl mx-8">
        <button
          onClick={() => setShowModelSwitcher(true)}
          className={clsx(
            'w-full flex items-center gap-3 px-4 py-2.5 rounded-xl',
            'bg-surface-100 dark:bg-surface-800',
            'border border-surface-200 dark:border-surface-700',
            'hover:border-brand-500 dark:hover:border-brand-500',
            'transition-all duration-200',
            'group'
          )}
        >
          <div className="flex items-center gap-2 flex-1">
            <div className={clsx(
              'w-8 h-8 rounded-lg flex items-center justify-center',
              activeModel
                ? 'bg-gradient-to-br from-brand-500/20 to-ai-purple/20'
                : 'bg-surface-200 dark:bg-surface-700'
            )}>
              <Cpu className={clsx(
                'w-4 h-4',
                activeModel ? 'text-brand-600 dark:text-brand-400' : 'text-surface-400'
              )} />
            </div>

            <div className="flex-1 text-left">
              <div className="flex items-center gap-2">
                <span className="text-sm font-medium text-surface-900 dark:text-surface-100">
                  {activeModel?.name || 'Select Model'}
                </span>
                {activeModel && (
                  <Badge variant="success" dot>
                    {activeModel.performance?.tokensPerSecond?.toFixed(0) || activeModel.throughput_toks?.toFixed(0) || '—'} tok/s
                  </Badge>
                )}
              </div>
              <span className="text-xs text-surface-500">
                {loadedModels.length} models loaded • Press{' '}
                <kbd className="px-1 py-0.5 bg-surface-200 dark:bg-surface-700 rounded text-2xs">⌘K</kbd>
                {' '}to switch
              </span>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Zap className="w-4 h-4 text-warning-500" />
            <span className="text-xs font-medium text-warning-600 dark:text-warning-400">
              &lt;1ms
            </span>
            <ChevronDown className="w-4 h-4 text-surface-400 group-hover:text-surface-600" />
          </div>
        </button>
      </div>

      {/* Right Section - Actions */}
      <div className="flex items-center gap-2">
        {/* Local-only indicator */}
        <div className="hidden lg:flex items-center">
          <Badge variant="info">Local-only</Badge>
        </div>

        {/* System Status */}
        {systemMetrics && (
          <div className="hidden lg:flex items-center gap-4 px-3 py-1.5 rounded-lg bg-surface-100 dark:bg-surface-800 mr-2">
            <div className="flex items-center gap-1.5">
              <Cpu className="w-3.5 h-3.5 text-surface-400" />
              <span className="text-xs text-surface-600 dark:text-surface-400">
                {systemMetrics.cpuUsage?.toFixed(0) || 0}%
              </span>
            </div>
            <div className="flex items-center gap-1.5">
              <HardDrive className="w-3.5 h-3.5 text-surface-400" />
              <span className="text-xs text-surface-600 dark:text-surface-400">
                {(systemMetrics.memoryTotal ? ((systemMetrics.memoryUsage || 0) / systemMetrics.memoryTotal) * 100 : 0).toFixed(0)}%
              </span>
            </div>
            {systemMetrics.gpuUsage !== undefined && (
              <div className="flex items-center gap-1.5">
                <Activity className="w-3.5 h-3.5 text-brand-500" />
                <span className="text-xs text-brand-600 dark:text-brand-400">
                  GPU {systemMetrics.gpuUsage?.toFixed(0) || 0}%
                </span>
              </div>
            )}
          </div>
        )}

        {/* Command Palette Trigger */}
        <IconButton
          icon={<Search className="w-5 h-5" />}
          label="Search (⌘K)"
          onClick={() => setCommandPaletteOpen(true)}
        />

        {/* Theme Switcher */}
        <div className="relative">
          <IconButton
            icon={theme === 'dark' ? <Moon className="w-5 h-5" /> : <Sun className="w-5 h-5" />}
            label="Toggle theme"
            onClick={() => setShowThemeMenu(!showThemeMenu)}
          />
          <AnimatePresence>
            {showThemeMenu && (
              <>
                <div className="fixed inset-0 z-40" onClick={() => setShowThemeMenu(false)} />
                <motion.div
                  initial={{ opacity: 0, y: -10 }}
                  animate={{ opacity: 1, y: 0 }}
                  exit={{ opacity: 0, y: -10 }}
                  className="absolute right-0 mt-2 w-36 card p-1 z-50"
                >
                  {themeOptions.map((option) => (
                    <button
                      key={option.value}
                      onClick={() => {
                        setTheme(option.value as 'light' | 'dark' | 'system');
                        setShowThemeMenu(false);
                      }}
                      className={clsx(
                        'w-full flex items-center gap-2 px-3 py-2 rounded-lg text-sm',
                        theme === option.value
                          ? 'bg-brand-50 dark:bg-brand-900/20 text-brand-600 dark:text-brand-400'
                          : 'hover:bg-surface-100 dark:hover:bg-surface-800 text-surface-700 dark:text-surface-300'
                      )}
                    >
                      <option.icon className="w-4 h-4" />
                      {option.label}
                      {theme === option.value && <Check className="w-4 h-4 ml-auto" />}
                    </button>
                  ))}
                </motion.div>
              </>
            )}
          </AnimatePresence>
        </div>

        {/* Notifications */}
        <div className="relative">
          <IconButton
            icon={
              <div className="relative">
                <Bell className="w-5 h-5" />
                {unreadNotifications > 0 && (
                  <span className="absolute -top-1 -right-1 w-4 h-4 bg-error-500 text-white text-2xs rounded-full flex items-center justify-center">
                    {unreadNotifications > 9 ? '9+' : unreadNotifications}
                  </span>
                )}
              </div>
            }
            label="Notifications"
            onClick={() => setShowNotifications(!showNotifications)}
          />
          {/* Notification dropdown would go here */}
        </div>
      </div>

      {/* Model Quick Switcher Modal */}
      <ModelQuickSwitcher
        isOpen={showModelSwitcher}
        onClose={() => setShowModelSwitcher(false)}
      />
    </header>
  );
};

export default Header;
