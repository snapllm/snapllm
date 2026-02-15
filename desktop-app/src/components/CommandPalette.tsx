// ============================================================================
// SnapLLM - Command Palette (⌘K)
// ============================================================================

import React, { useState, useEffect, useRef, useMemo } from 'react';
import { useNavigate } from 'react-router-dom';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Search,
  LayoutDashboard,
  MessageSquare,
  Image,
  Eye,
  Boxes,
  GitCompare,
  Zap,
  Code2,
  BarChart3,
  Settings,
  Info,
  HelpCircle,
  Moon,
  Sun,
  Plus,
  ArrowRight,
  Command,
  Cpu,
} from 'lucide-react';
import { useAppStore, useModelStore, useChatStore } from '../store';
import { Kbd } from './ui';

interface CommandItem {
  id: string;
  title: string;
  description?: string;
  icon: React.ComponentType<{ className?: string }>;
  category: string;
  keywords?: string[];
  shortcut?: string;
  action: () => void;
}

interface CommandPaletteProps {
  isOpen: boolean;
  onClose: () => void;
}

export const CommandPalette: React.FC<CommandPaletteProps> = ({ isOpen, onClose }) => {
  const navigate = useNavigate();
  const { setTheme } = useAppStore();
  const { models, activeModelId, setActiveModel } = useModelStore();
  const { createNewConversation } = useChatStore();

  const [query, setQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);

  // Define all commands
  const commands: CommandItem[] = useMemo(() => [
    // Navigation
    { id: 'nav-dashboard', title: 'Go to Dashboard', icon: LayoutDashboard, category: 'Navigation', action: () => { navigate('/'); onClose(); } },
    { id: 'nav-chat', title: 'Go to Chat', icon: MessageSquare, category: 'Navigation', shortcut: '⌘1', action: () => { navigate('/chat'); onClose(); } },
    { id: 'nav-images', title: 'Go to Image Studio', icon: Image, category: 'Navigation', shortcut: '⌘2', action: () => { navigate('/images'); onClose(); } },
    { id: 'nav-vision', title: 'Go to Vision', icon: Eye, category: 'Navigation', action: () => { navigate('/vision'); onClose(); } },
    { id: 'nav-models', title: 'Go to Model Hub', icon: Boxes, category: 'Navigation', shortcut: '⌘M', action: () => { navigate('/models'); onClose(); } },
    { id: 'nav-compare', title: 'Go to A/B Compare', icon: GitCompare, category: 'Navigation', action: () => { navigate('/compare'); onClose(); } },
    { id: 'nav-switch', title: 'Go to Quick Switch', icon: Zap, category: 'Navigation', action: () => { navigate('/switch'); onClose(); } },
    { id: 'nav-playground', title: 'Go to API Playground', icon: Code2, category: 'Navigation', action: () => { navigate('/playground'); onClose(); } },
    { id: 'nav-metrics', title: 'Go to Metrics', icon: BarChart3, category: 'Navigation', action: () => { navigate('/metrics'); onClose(); } },
    { id: 'nav-settings', title: 'Go to Server Settings', icon: Settings, category: 'Navigation', shortcut: '⌘,', action: () => { navigate('/settings'); onClose(); } },
    { id: 'nav-about', title: 'Go to About', icon: Info, category: 'Navigation', action: () => { navigate('/about'); onClose(); } },
    { id: 'nav-help', title: 'Go to Help & Docs', icon: HelpCircle, category: 'Navigation', action: () => { navigate('/help'); onClose(); } },

    // Actions
    { id: 'action-new-chat', title: 'New Conversation', description: 'Start a new chat', icon: Plus, category: 'Actions', shortcut: '⌘N', action: () => { if (activeModelId) createNewConversation(activeModelId); navigate('/chat'); onClose(); } },
    { id: 'action-theme-light', title: 'Switch to Light Mode', icon: Sun, category: 'Theme', action: () => { setTheme('light'); onClose(); } },
    { id: 'action-theme-dark', title: 'Switch to Dark Mode', icon: Moon, category: 'Theme', action: () => { setTheme('dark'); onClose(); } },

    // Model switching - dynamically add loaded models
    ...models.filter(m => m.status === 'ready').map(model => ({
      id: `model-${model.id}`,
      title: `Switch to ${model.name}`,
      description: `${model.type.toUpperCase()} • ${model.quantization}`,
      icon: Cpu,
      category: 'Models',
      keywords: [model.name, model.type, model.quantization || '', ...model.tags],
      action: () => { setActiveModel(model.id); onClose(); },
    })),
  ], [navigate, onClose, models, activeModelId, createNewConversation, setTheme]);

  // Filter commands based on query
  const filteredCommands = useMemo(() => {
    if (!query) return commands;
    const lowerQuery = query.toLowerCase();
    return commands.filter(cmd =>
      cmd.title.toLowerCase().includes(lowerQuery) ||
      cmd.description?.toLowerCase().includes(lowerQuery) ||
      cmd.category.toLowerCase().includes(lowerQuery) ||
      cmd.keywords?.some(k => k.toLowerCase().includes(lowerQuery))
    );
  }, [commands, query]);

  // Group commands by category
  const groupedCommands = useMemo(() => {
    const groups: Record<string, CommandItem[]> = {};
    filteredCommands.forEach(cmd => {
      if (!groups[cmd.category]) groups[cmd.category] = [];
      groups[cmd.category].push(cmd);
    });
    return groups;
  }, [filteredCommands]);

  // Reset state when opened
  useEffect(() => {
    if (isOpen) {
      setQuery('');
      setSelectedIndex(0);
      setTimeout(() => inputRef.current?.focus(), 0);
    }
  }, [isOpen]);

  // Keyboard navigation
  useEffect(() => {
    if (!isOpen) return;

    const handleKeyDown = (e: KeyboardEvent) => {
      switch (e.key) {
        case 'ArrowDown':
          e.preventDefault();
          setSelectedIndex(i => Math.min(i + 1, filteredCommands.length - 1));
          break;
        case 'ArrowUp':
          e.preventDefault();
          setSelectedIndex(i => Math.max(i - 1, 0));
          break;
        case 'Enter':
          e.preventDefault();
          filteredCommands[selectedIndex]?.action();
          break;
        case 'Escape':
          onClose();
          break;
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, selectedIndex, filteredCommands, onClose]);

  // Update selected index when filtered results change
  useEffect(() => {
    setSelectedIndex(0);
  }, [query]);

  let flatIndex = 0;

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

          {/* Command Palette */}
          <motion.div
            initial={{ opacity: 0, scale: 0.95, y: -20 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.95, y: -20 }}
            transition={{ duration: 0.15 }}
            className="fixed inset-x-4 top-[15vh] mx-auto max-w-xl z-50"
          >
            <div className="card overflow-hidden shadow-elevation-4">
              {/* Search Input */}
              <div className="flex items-center gap-3 px-4 py-3 border-b border-surface-200 dark:border-surface-800">
                <Command className="w-5 h-5 text-surface-400" />
                <input
                  ref={inputRef}
                  type="text"
                  placeholder="Type a command or search..."
                  value={query}
                  onChange={(e) => setQuery(e.target.value)}
                  className="flex-1 bg-transparent text-base focus:outline-none placeholder:text-surface-400"
                />
                <Kbd>esc</Kbd>
              </div>

              {/* Results */}
              <div className="max-h-[50vh] overflow-y-auto">
                {Object.entries(groupedCommands).map(([category, items]) => (
                  <div key={category} className="py-2">
                    <p className="px-4 py-1.5 text-xs font-medium text-surface-500 uppercase tracking-wider">
                      {category}
                    </p>
                    {items.map((cmd) => {
                      const currentIndex = flatIndex++;
                      const isSelected = currentIndex === selectedIndex;

                      return (
                        <button
                          key={cmd.id}
                          onClick={cmd.action}
                          className={clsx(
                            'w-full flex items-center gap-3 px-4 py-2.5 transition-colors',
                            isSelected
                              ? 'bg-brand-50 dark:bg-brand-900/20'
                              : 'hover:bg-surface-100 dark:hover:bg-surface-800'
                          )}
                        >
                          <cmd.icon className={clsx(
                            'w-5 h-5',
                            isSelected ? 'text-brand-600 dark:text-brand-400' : 'text-surface-500'
                          )} />
                          <div className="flex-1 text-left">
                            <p className={clsx(
                              'text-sm font-medium',
                              isSelected ? 'text-brand-700 dark:text-brand-300' : 'text-surface-900 dark:text-surface-100'
                            )}>
                              {cmd.title}
                            </p>
                            {cmd.description && (
                              <p className="text-xs text-surface-500">{cmd.description}</p>
                            )}
                          </div>
                          {cmd.shortcut && (
                            <Kbd>{cmd.shortcut}</Kbd>
                          )}
                          {isSelected && (
                            <ArrowRight className="w-4 h-4 text-brand-500" />
                          )}
                        </button>
                      );
                    })}
                  </div>
                ))}

                {/* Empty State */}
                {filteredCommands.length === 0 && (
                  <div className="py-12 text-center">
                    <Search className="w-10 h-10 text-surface-300 dark:text-surface-600 mx-auto mb-3" />
                    <p className="text-surface-900 dark:text-surface-100 font-medium">No commands found</p>
                    <p className="text-sm text-surface-500">Try a different search term</p>
                  </div>
                )}
              </div>

              {/* Footer */}
              <div className="flex items-center justify-between px-4 py-2.5 bg-surface-50 dark:bg-surface-800/50 border-t border-surface-200 dark:border-surface-800">
                <div className="flex items-center gap-4 text-xs text-surface-500">
                  <span className="flex items-center gap-1">
                    <Kbd>↑</Kbd><Kbd>↓</Kbd> navigate
                  </span>
                  <span className="flex items-center gap-1">
                    <Kbd>↵</Kbd> select
                  </span>
                </div>
                <span className="text-xs text-surface-400">
                  {filteredCommands.length} commands
                </span>
              </div>
            </div>
          </motion.div>
        </>
      )}
    </AnimatePresence>
  );
};

export default CommandPalette;
