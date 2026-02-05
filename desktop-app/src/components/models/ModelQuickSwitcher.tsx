// ============================================================================
// SnapLLM Enterprise - Model Quick Switcher
// Ultra-fast <1ms model switching UI
// ============================================================================

import React, { useState, useEffect, useRef, useMemo } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Search,
  Zap,
  Cpu,
  MessageSquare,
  Image,
  Eye,
  Clock,
  HardDrive,
  Check,
  Plus,
  Star,
  StarOff,
  ArrowRight,
  Activity,
  Layers,
  X,
} from 'lucide-react';
import { useModelStore } from '../../store';
import { Badge, Kbd, Spinner } from '../ui';
import type { Model, ModelType } from '../../types';

interface ModelQuickSwitcherProps {
  isOpen: boolean;
  onClose: () => void;
}

const modelTypeIcons: Record<ModelType, React.ComponentType<{ className?: string }>> = {
  llm: MessageSquare,
  diffusion: Image,
  vision: Eye,
  embedding: Layers,
};

const modelTypeColors: Record<ModelType, string> = {
  llm: 'from-brand-500 to-ai-purple',
  diffusion: 'from-ai-pink to-ai-orange',
  vision: 'from-ai-cyan to-ai-emerald',
  embedding: 'from-surface-500 to-surface-600',
};

export const ModelQuickSwitcher: React.FC<ModelQuickSwitcherProps> = ({
  isOpen,
  onClose,
}) => {
  const { models, activeModelId, setActiveModel, loadingModels } = useModelStore();
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const [recentModels, setRecentModels] = useState<string[]>([]);
  const [favoriteModels, setFavoriteModels] = useState<string[]>([]);
  const [switchTime, setSwitchTime] = useState<number | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  // Filter models based on search
  const filteredModels = useMemo(() => {
    const loaded = models.filter((m) => m.status === 'ready');
    if (!searchQuery) return loaded;

    const query = searchQuery.toLowerCase();
    return loaded.filter((m) =>
      m.name.toLowerCase().includes(query) ||
      m.type.toLowerCase().includes(query) ||
      m.tags.some((t) => t.toLowerCase().includes(query)) ||
      m.quantization?.toLowerCase().includes(query)
    );
  }, [models, searchQuery]);

  // Group models by type
  const groupedModels = useMemo(() => {
    const groups: Record<ModelType, Model[]> = {
      llm: [],
      diffusion: [],
      vision: [],
      embedding: [],
    };
    filteredModels.forEach((m) => {
      if (groups[m.type]) {
        groups[m.type].push(m);
      }
    });
    return groups;
  }, [filteredModels]);

  // Get favorite and recent models
  const favoriteModelsList = useMemo(() =>
    filteredModels.filter((m) => favoriteModels.includes(m.id)),
  [filteredModels, favoriteModels]);

  const recentModelsList = useMemo(() =>
    recentModels
      .map((id) => filteredModels.find((m) => m.id === id))
      .filter((m): m is Model => !!m && !favoriteModels.includes(m.id))
      .slice(0, 3),
  [recentModels, filteredModels, favoriteModels]);

  // Focus input when opened
  useEffect(() => {
    if (isOpen) {
      inputRef.current?.focus();
      setSearchQuery('');
      setSelectedIndex(0);
      setSwitchTime(null);
    }
  }, [isOpen]);

  // Keyboard navigation
  useEffect(() => {
    if (!isOpen) return;

    const handleKeyDown = (e: KeyboardEvent) => {
      switch (e.key) {
        case 'ArrowDown':
          e.preventDefault();
          setSelectedIndex((i) => Math.min(i + 1, filteredModels.length - 1));
          break;
        case 'ArrowUp':
          e.preventDefault();
          setSelectedIndex((i) => Math.max(i - 1, 0));
          break;
        case 'Enter':
          e.preventDefault();
          if (filteredModels[selectedIndex]) {
            handleModelSwitch(filteredModels[selectedIndex]);
          }
          break;
        case 'Escape':
          onClose();
          break;
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, selectedIndex, filteredModels, onClose]);

  // Handle model switch with timing
  const handleModelSwitch = async (model: Model) => {
    if (model.id === activeModelId) {
      onClose();
      return;
    }

    const startTime = performance.now();
    setActiveModel(model.id);
    const endTime = performance.now();
    const switchDuration = endTime - startTime;
    setSwitchTime(switchDuration);

    // Add to recent models
    setRecentModels((prev) => {
      const updated = [model.id, ...prev.filter((id) => id !== model.id)].slice(0, 10);
      localStorage.setItem('snapllm-recent-models', JSON.stringify(updated));
      return updated;
    });

    // Show switch time briefly then close
    setTimeout(() => {
      onClose();
    }, 800);
  };

  // Toggle favorite
  const toggleFavorite = (modelId: string, e: React.MouseEvent) => {
    e.stopPropagation();
    setFavoriteModels((prev) => {
      const updated = prev.includes(modelId)
        ? prev.filter((id) => id !== modelId)
        : [...prev, modelId];
      localStorage.setItem('snapllm-favorite-models', JSON.stringify(updated));
      return updated;
    });
  };

  // Load favorites and recents from localStorage
  useEffect(() => {
    const savedFavorites = localStorage.getItem('snapllm-favorite-models');
    const savedRecents = localStorage.getItem('snapllm-recent-models');
    if (savedFavorites) setFavoriteModels(JSON.parse(savedFavorites));
    if (savedRecents) setRecentModels(JSON.parse(savedRecents));
  }, []);

  return (
    <AnimatePresence>
      {isOpen && (
        <>
          {/* Backdrop */}
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            className="fixed inset-0 z-50 bg-black/50 backdrop-blur-sm"
            onClick={onClose}
          />

          {/* Modal */}
          <motion.div
            initial={{ opacity: 0, scale: 0.95, y: -20 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.95, y: -20 }}
            transition={{ duration: 0.2 }}
            className="fixed inset-x-4 top-[10vh] mx-auto max-w-2xl z-50"
          >
            <div className="card overflow-hidden shadow-elevation-4">
              {/* Search Header */}
              <div className="relative border-b border-surface-200 dark:border-surface-800">
                <Search className="absolute left-4 top-1/2 -translate-y-1/2 w-5 h-5 text-surface-400" />
                <input
                  ref={inputRef}
                  type="text"
                  placeholder="Search models by name, type, or tag..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="w-full pl-12 pr-4 py-4 bg-transparent text-lg focus:outline-none placeholder:text-surface-400"
                />
                <div className="absolute right-4 top-1/2 -translate-y-1/2 flex items-center gap-2">
                  <Kbd>esc</Kbd>
                </div>
              </div>

              {/* Switch Time Indicator */}
              <AnimatePresence>
                {switchTime !== null && (
                  <motion.div
                    initial={{ opacity: 0, height: 0 }}
                    animate={{ opacity: 1, height: 'auto' }}
                    exit={{ opacity: 0, height: 0 }}
                    className="bg-success-50 dark:bg-success-900/20 border-b border-success-200 dark:border-success-800"
                  >
                    <div className="flex items-center justify-center gap-2 py-3">
                      <Zap className="w-5 h-5 text-success-500" />
                      <span className="text-success-700 dark:text-success-400 font-medium">
                        Model switched in {switchTime.toFixed(2)}ms
                      </span>
                      <Badge variant="success">Ultra-fast</Badge>
                    </div>
                  </motion.div>
                )}
              </AnimatePresence>

              {/* Model List */}
              <div className="max-h-[60vh] overflow-y-auto">
                {/* Active Model */}
                {activeModelId && (
                  <div className="p-3 border-b border-surface-200 dark:border-surface-800">
                    <p className="text-xs font-medium text-surface-500 uppercase tracking-wider mb-2 px-2">
                      Currently Active
                    </p>
                    {models.filter((m) => m.id === activeModelId).map((model) => (
                      <ModelItem
                        key={model.id}
                        model={model}
                        isActive={true}
                        isSelected={false}
                        isFavorite={favoriteModels.includes(model.id)}
                        onSelect={() => onClose()}
                        onToggleFavorite={toggleFavorite}
                      />
                    ))}
                  </div>
                )}

                {/* Favorites */}
                {favoriteModelsList.length > 0 && (
                  <div className="p-3 border-b border-surface-200 dark:border-surface-800">
                    <p className="text-xs font-medium text-surface-500 uppercase tracking-wider mb-2 px-2">
                      <Star className="w-3 h-3 inline mr-1" />
                      Favorites
                    </p>
                    {favoriteModelsList.map((model, index) => (
                      <ModelItem
                        key={model.id}
                        model={model}
                        isActive={model.id === activeModelId}
                        isSelected={selectedIndex === index}
                        isFavorite={true}
                        onSelect={() => handleModelSwitch(model)}
                        onToggleFavorite={toggleFavorite}
                      />
                    ))}
                  </div>
                )}

                {/* Recent */}
                {recentModelsList.length > 0 && !searchQuery && (
                  <div className="p-3 border-b border-surface-200 dark:border-surface-800">
                    <p className="text-xs font-medium text-surface-500 uppercase tracking-wider mb-2 px-2">
                      <Clock className="w-3 h-3 inline mr-1" />
                      Recent
                    </p>
                    {recentModelsList.map((model, index) => (
                      <ModelItem
                        key={model.id}
                        model={model}
                        isActive={model.id === activeModelId}
                        isSelected={selectedIndex === favoriteModelsList.length + index}
                        isFavorite={false}
                        onSelect={() => handleModelSwitch(model)}
                        onToggleFavorite={toggleFavorite}
                      />
                    ))}
                  </div>
                )}

                {/* All Models by Type */}
                {Object.entries(groupedModels).map(([type, typeModels]) => {
                  if (typeModels.length === 0) return null;
                  const TypeIcon = modelTypeIcons[type as ModelType];

                  return (
                    <div key={type} className="p-3 border-b border-surface-200 dark:border-surface-800 last:border-0">
                      <p className="text-xs font-medium text-surface-500 uppercase tracking-wider mb-2 px-2 flex items-center gap-1">
                        <TypeIcon className="w-3 h-3" />
                        {type.toUpperCase()} Models
                        <Badge variant="default" className="ml-2">{typeModels.length}</Badge>
                      </p>
                      {typeModels.map((model) => (
                        <ModelItem
                          key={model.id}
                          model={model}
                          isActive={model.id === activeModelId}
                          isSelected={filteredModels.indexOf(model) === selectedIndex}
                          isFavorite={favoriteModels.includes(model.id)}
                          onSelect={() => handleModelSwitch(model)}
                          onToggleFavorite={toggleFavorite}
                        />
                      ))}
                    </div>
                  );
                })}

                {/* Empty State */}
                {filteredModels.length === 0 && (
                  <div className="p-12 text-center">
                    <Cpu className="w-12 h-12 text-surface-300 dark:text-surface-600 mx-auto mb-4" />
                    <p className="text-surface-900 dark:text-surface-100 font-medium">No models found</p>
                    <p className="text-surface-500 text-sm mt-1">
                      {searchQuery ? 'Try a different search term' : 'Load a model to get started'}
                    </p>
                  </div>
                )}
              </div>

              {/* Footer */}
              <div className="flex items-center justify-between px-4 py-3 bg-surface-50 dark:bg-surface-800/50 border-t border-surface-200 dark:border-surface-800">
                <div className="flex items-center gap-4 text-xs text-surface-500">
                  <span className="flex items-center gap-1">
                    <Kbd>↑</Kbd><Kbd>↓</Kbd> navigate
                  </span>
                  <span className="flex items-center gap-1">
                    <Kbd>↵</Kbd> select
                  </span>
                  <span className="flex items-center gap-1">
                    <Kbd>esc</Kbd> close
                  </span>
                </div>
                <div className="flex items-center gap-2 text-xs">
                  <Zap className="w-4 h-4 text-warning-500" />
                  <span className="text-warning-600 dark:text-warning-400 font-medium">
                    vPID: &lt;1ms switching
                  </span>
                </div>
              </div>
            </div>
          </motion.div>
        </>
      )}
    </AnimatePresence>
  );
};

// Model Item Component
interface ModelItemProps {
  model: Model;
  isActive: boolean;
  isSelected: boolean;
  isFavorite: boolean;
  onSelect: () => void;
  onToggleFavorite: (id: string, e: React.MouseEvent) => void;
}

const ModelItem: React.FC<ModelItemProps> = ({
  model,
  isActive,
  isSelected,
  isFavorite,
  onSelect,
  onToggleFavorite,
}) => {
  const TypeIcon = modelTypeIcons[model.type];
  const gradientColor = modelTypeColors[model.type];

  return (
    <button
      onClick={onSelect}
      className={clsx(
        'w-full flex items-center gap-3 p-3 rounded-xl transition-all duration-150',
        'group',
        isSelected && 'bg-brand-50 dark:bg-brand-900/20',
        !isSelected && 'hover:bg-surface-100 dark:hover:bg-surface-800',
        isActive && 'ring-2 ring-brand-500 ring-offset-2 dark:ring-offset-surface-900'
      )}
    >
      {/* Model Icon */}
      <div className={clsx(
        'w-10 h-10 rounded-xl flex items-center justify-center',
        `bg-gradient-to-br ${gradientColor}`
      )}>
        <TypeIcon className="w-5 h-5 text-white" />
      </div>

      {/* Model Info */}
      <div className="flex-1 text-left">
        <div className="flex items-center gap-2">
          <span className="font-medium text-surface-900 dark:text-surface-100">
            {model.name}
          </span>
          {isActive && (
            <Badge variant="success" dot>Active</Badge>
          )}
          {model.status === 'loading' && (
            <Spinner size="sm" />
          )}
        </div>
        <div className="flex items-center gap-3 text-xs text-surface-500 mt-0.5">
          <span className="flex items-center gap-1">
            <Cpu className="w-3 h-3" />
            {model.quantization}
          </span>
          <span className="flex items-center gap-1">
            <Activity className="w-3 h-3" />
            {model.performance.tokensPerSecond.toFixed(0)} tok/s
          </span>
          <span className="flex items-center gap-1">
            <HardDrive className="w-3 h-3" />
            {(model.performance.memoryUsageMb / 1024).toFixed(1)} GB
          </span>
        </div>
      </div>

      {/* Actions */}
      <div className="flex items-center gap-2">
        <button
          onClick={(e) => onToggleFavorite(model.id, e)}
          className={clsx(
            'p-1.5 rounded-lg transition-colors',
            isFavorite
              ? 'text-warning-500 hover:bg-warning-100 dark:hover:bg-warning-900/20'
              : 'text-surface-400 hover:text-warning-500 hover:bg-surface-100 dark:hover:bg-surface-800 opacity-0 group-hover:opacity-100'
          )}
        >
          {isFavorite ? <Star className="w-4 h-4 fill-current" /> : <StarOff className="w-4 h-4" />}
        </button>
        <ArrowRight className={clsx(
          'w-4 h-4 transition-transform',
          isSelected ? 'text-brand-500 translate-x-0' : 'text-surface-400 -translate-x-2 opacity-0 group-hover:opacity-100 group-hover:translate-x-0'
        )} />
      </div>
    </button>
  );
};

export default ModelQuickSwitcher;
