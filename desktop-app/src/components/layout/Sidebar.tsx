// ============================================================================
// SnapLLM - Sidebar Navigation
// ============================================================================

import React from 'react';
import { NavLink, useLocation } from 'react-router-dom';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  LayoutDashboard,
  MessageSquare,
  Image,
  Eye,
  Boxes,
  GitCompare,
  Code2,
  Settings,
  Info,
  Layers,
  HelpCircle,
  ChevronLeft,
  ChevronRight,
  Zap,
  Cpu,
} from 'lucide-react';
import { useAppStore, useModelStore, useMetricsStore } from '../../store';
import { Badge, Tooltip, Avatar } from '../ui';

// Navigation items grouped by category
const navigationGroups = [
  {
    id: 'overview',
    label: 'Overview',
    items: [
      { path: '/', icon: LayoutDashboard, label: 'Dashboard', description: 'System overview & health' },
    ],
  },
  {
    id: 'genai',
    label: 'GenAI Studio',
    items: [
      { path: '/chat', icon: MessageSquare, label: 'Chat', description: 'Text generation & conversations', badge: 'LLM' },
      { path: '/images', icon: Image, label: 'Images', description: 'Stable Diffusion generation', badge: 'SD' },
      { path: '/vision', icon: Eye, label: 'Vision', description: 'Multimodal image analysis', badge: 'VLM' },
    ],
  },
  {
    id: 'models',
    label: 'Model Hub',
    items: [
      { path: '/models', icon: Boxes, label: 'Models', description: 'Load & manage models' },
      { path: '/compare', icon: GitCompare, label: 'A/B Compare', description: 'Side-by-side model comparison' },
      { path: '/switch', icon: Zap, label: 'Quick Switch', description: '<1ms model switching' },
    ],
  },
  {
    id: 'advanced',
    label: 'Advanced',
    items: [
      { path: '/contexts', icon: Layers, label: 'vPID L2 Contexts', description: 'KV cache management', badge: 'NEW' },
      // RAG, Agents, Workflows removed - no backend implementation yet
    ],
  },
  {
    id: 'developer',
    label: 'Developer',
    items: [
      { path: '/playground', icon: Code2, label: 'API Playground', description: 'Interactive API testing' },
      { path: '/batch', icon: Layers, label: 'Batch Processing', description: 'Process multiple prompts' },
      // Metrics, API Keys removed - no backend implementation
    ],
  },
  // Enterprise section removed - no backend implementation (Team, Audit, Security)
];

const bottomNavItems = [
  { path: '/settings', icon: Settings, label: 'Server Settings' },
  { path: '/about', icon: Info, label: 'About' },
  { path: '/help', icon: HelpCircle, label: 'Help & Docs' },
];

export const Sidebar: React.FC = () => {
  const location = useLocation();
  const { sidebarCollapsed, toggleSidebar } = useAppStore();
  const { models, activeModelId } = useModelStore();
  const { systemMetrics, isConnected } = useMetricsStore();

  const activeModel = models.find((m) => m.id === activeModelId);
  const loadedModelsCount = models.filter((m) => m.status === 'ready').length;

  return (
    <aside
      className={clsx(
        'sidebar flex flex-col',
        sidebarCollapsed && 'sidebar-collapsed'
      )}
    >
      {/* Logo & Brand */}
      <div className={clsx(
        "flex items-center border-b border-surface-200 dark:border-surface-800",
        sidebarCollapsed ? "flex-col h-24 py-3 px-2" : "justify-between h-20 px-4"
      )}>
        <AnimatePresence mode="wait">
          {!sidebarCollapsed ? (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="flex items-end gap-1"
            >
              <img
                src="/snapllm-logo.png"
                alt="SnapLLM"
                className="h-12 w-auto"
              />
              <span className="text-[10px] font-bold text-orange-500 tracking-wider mb-1">(BETA)</span>
            </motion.div>
          ) : (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="relative flex items-center justify-center"
            >
              <img
                src="/snapllm-icon.png"
                alt="SnapLLM"
                className="h-12 w-12"
              />
              <span className="absolute -bottom-1 -right-1 text-[7px] font-bold text-orange-500">(BETA)</span>
            </motion.div>
          )}
        </AnimatePresence>

        <button
          onClick={toggleSidebar}
          className={clsx(
            "p-1.5 rounded-lg hover:bg-surface-100 dark:hover:bg-surface-800 text-surface-500",
            sidebarCollapsed && "mt-1"
          )}
        >
          {sidebarCollapsed ? (
            <ChevronRight className="w-4 h-4" />
          ) : (
            <ChevronLeft className="w-4 h-4" />
          )}
        </button>
      </div>

      {/* Active Model Indicator */}
      <div className="p-3 border-b border-surface-200 dark:border-surface-800">
        <AnimatePresence mode="wait">
          {!sidebarCollapsed ? (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="card-gradient p-3"
            >
              <div className="flex items-center justify-between mb-2">
                <span className="text-2xs font-medium text-surface-500 uppercase tracking-wider">Active Model</span>
                <Badge variant="success" dot>{loadedModelsCount} loaded</Badge>
              </div>
              {activeModel ? (
                <div className="flex items-center gap-2">
                  <div className="w-8 h-8 rounded-lg bg-brand-500/20 flex items-center justify-center">
                    <Cpu className="w-4 h-4 text-brand-600 dark:text-brand-400" />
                  </div>
                  <div className="flex-1 min-w-0">
                    <p className="text-sm font-medium text-surface-900 dark:text-surface-100 truncate">
                      {activeModel.name}
                    </p>
                    <p className="text-2xs text-surface-500 truncate">
                      {activeModel.quantization || activeModel.engine || 'GPU'} • {activeModel.performance?.tokensPerSecond?.toFixed(0) || activeModel.throughput_toks?.toFixed(0) || '—'} tok/s
                    </p>
                  </div>
                  <div className="status-online" />
                </div>
              ) : (
                <div className="text-sm text-surface-500">No model selected</div>
              )}
            </motion.div>
          ) : (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="flex justify-center"
            >
              <Tooltip content={activeModel?.name || 'No model'} position="right">
                <div className={clsx(
                  'w-10 h-10 rounded-lg flex items-center justify-center',
                  activeModel ? 'bg-brand-500/20' : 'bg-surface-200 dark:bg-surface-800'
                )}>
                  <Cpu className={clsx(
                    'w-5 h-5',
                    activeModel ? 'text-brand-600' : 'text-surface-400'
                  )} />
                </div>
              </Tooltip>
            </motion.div>
          )}
        </AnimatePresence>
      </div>

      {/* Navigation */}
      <nav className="flex-1 overflow-y-auto py-3 px-2 space-y-6 scrollbar-thin">
        {navigationGroups.map((group) => (
          <div key={group.id}>
            <AnimatePresence>
              {!sidebarCollapsed && (
                <motion.h3
                  initial={{ opacity: 0 }}
                  animate={{ opacity: 1 }}
                  exit={{ opacity: 0 }}
                  className="px-3 mb-2 text-2xs font-semibold text-surface-400 uppercase tracking-wider"
                >
                  {group.label}
                </motion.h3>
              )}
            </AnimatePresence>
            <ul className="space-y-1">
              {group.items.map((item) => (
                <li key={item.path}>
                  <NavItem
                    {...item}
                    isActive={location.pathname === item.path}
                    isCollapsed={sidebarCollapsed}
                  />
                </li>
              ))}
            </ul>
          </div>
        ))}
      </nav>

      {/* Bottom Navigation */}
      <div className="border-t border-surface-200 dark:border-surface-800 p-2 space-y-1">
        {bottomNavItems.map((item) => (
          <NavItem
            key={item.path}
            {...item}
            isActive={location.pathname === item.path}
            isCollapsed={sidebarCollapsed}
          />
        ))}
      </div>

      {/* System Status */}
      <div className="border-t border-surface-200 dark:border-surface-800 p-3">
        <AnimatePresence mode="wait">
          {!sidebarCollapsed ? (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="flex items-center justify-between"
            >
              <div className="flex items-center gap-2">
                <div className={clsx(
                  'w-2 h-2 rounded-full',
                  isConnected ? 'bg-success-500 animate-pulse' : 'bg-error-500'
                )} />
                <span className="text-xs text-surface-500">
                  {isConnected ? 'Connected' : 'Disconnected'}
                </span>
              </div>
              {systemMetrics && (
                <span className="text-2xs text-surface-400">
                  GPU: {systemMetrics.gpuUsage?.toFixed(0) || 0}%
                </span>
              )}
            </motion.div>
          ) : (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="flex justify-center"
            >
              <div className={clsx(
                'w-3 h-3 rounded-full',
                isConnected ? 'bg-success-500 animate-pulse' : 'bg-error-500'
              )} />
            </motion.div>
          )}
        </AnimatePresence>
      </div>
    </aside>
  );
};

// Navigation Item Component
interface NavItemProps {
  path: string;
  icon: React.ComponentType<{ className?: string }>;
  label: string;
  description?: string;
  badge?: string;
  isActive: boolean;
  isCollapsed: boolean;
}

const NavItem: React.FC<NavItemProps> = ({
  path,
  icon: Icon,
  label,
  description,
  badge,
  isActive,
  isCollapsed,
}) => {
  const content = (
    <NavLink
      to={path}
      className={clsx(
        'flex items-center gap-3 px-3 py-2.5 rounded-lg transition-all duration-200',
        'group relative',
        isActive
          ? 'bg-brand-50 dark:bg-brand-900/20 text-brand-600 dark:text-brand-400 font-medium'
          : 'text-surface-600 dark:text-surface-400 hover:bg-surface-100 dark:hover:bg-surface-800',
        isCollapsed && 'justify-center'
      )}
    >
      {/* Active indicator */}
      {isActive && (
        <motion.div
          layoutId="activeIndicator"
          className="absolute left-0 top-1/2 -translate-y-1/2 w-1 h-6 bg-brand-500 rounded-r-full"
        />
      )}

      <Icon className={clsx(
        'w-5 h-5 flex-shrink-0',
        isActive ? 'text-brand-600 dark:text-brand-400' : 'text-surface-500 group-hover:text-surface-700 dark:group-hover:text-surface-300'
      )} />

      <AnimatePresence>
        {!isCollapsed && (
          <motion.div
            initial={{ opacity: 0, width: 0 }}
            animate={{ opacity: 1, width: 'auto' }}
            exit={{ opacity: 0, width: 0 }}
            className="flex-1 flex items-center justify-between overflow-hidden"
          >
            <span className="truncate">{label}</span>
            {badge && (
              <Badge
                variant={badge === 'NEW' ? 'success' : 'brand'}
                className="ml-2"
              >
                {badge}
              </Badge>
            )}
          </motion.div>
        )}
      </AnimatePresence>
    </NavLink>
  );

  if (isCollapsed) {
    return (
      <Tooltip content={label} position="right">
        {content}
      </Tooltip>
    );
  }

  return content;
};

export default Sidebar;
