// ============================================================================
// SnapLLM Enterprise - Main Layout Component
// ============================================================================

import React, { useEffect } from 'react';
import { Outlet } from 'react-router-dom';
import { clsx } from 'clsx';
import { Toaster } from 'react-hot-toast';
import { useAppStore, useModelStore, useMetricsStore } from '../../store';
import { useModels, useHealth } from '../../hooks/useApi';
import { Sidebar } from './Sidebar';
import { Header } from './Header';
import { CommandPalette } from '../CommandPalette';
import type { Model, ModelType } from '../../types';

export const Layout: React.FC = () => {
  const { theme, sidebarCollapsed, commandPaletteOpen, setCommandPaletteOpen } = useAppStore();
  const { setModels, setActiveModel } = useModelStore();
  const { setIsConnected } = useMetricsStore();

  // Fetch models from API
  const { data: modelsResponse } = useModels();
  const { data: healthResponse, isError: healthError } = useHealth();

  // Sync API models to Zustand store
  useEffect(() => {
    if (modelsResponse?.models) {
      // Transform API ModelInfo to store Model type
      const transformedModels: Model[] = modelsResponse.models.map((apiModel) => {
        const apiType = (apiModel.type || 'llm').toLowerCase();
        const mappedType: ModelType =
          apiType === 'diffusion' ? 'diffusion' :
          apiType === 'vision' ? 'vision' :
          apiType === 'embedding' ? 'embedding' :
          'llm';

        return ({
        id: apiModel.id,
        name: apiModel.name,
        type: mappedType,
        status: apiModel.status === 'loaded' ? 'ready' : 'idle',
        path: '', // API doesn't provide this
        size: (apiModel.ram_usage_mb || 0) * 1024 * 1024, // Convert MB to bytes
        quantization: apiModel.strategy || 'Q8_0',
        tags: [apiModel.domain || 'general'],
        metadata: {
          family: apiModel.engine || 'vpid',
        },
        performance: {
          tokensPerSecond: apiModel.throughput_toks || 0,
          loadTimeMs: apiModel.load_time_ms || 0,
          memoryUsageMb: apiModel.ram_usage_mb || 0,
        },
        loadedAt: apiModel.loaded_at,
      });
    });

      setModels(transformedModels);

      // Set active model
      if (modelsResponse.current_model) {
        setActiveModel(modelsResponse.current_model);
      }
    }
  }, [modelsResponse, setModels, setActiveModel]);

  // Sync connection status
  useEffect(() => {
    setIsConnected(!healthError && healthResponse?.status === 'ok');
  }, [healthResponse, healthError, setIsConnected]);

  // Apply theme to document
  useEffect(() => {
    const root = document.documentElement;

    if (theme === 'system') {
      const isDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      root.classList.toggle('dark', isDark);
    } else {
      root.classList.toggle('dark', theme === 'dark');
    }
  }, [theme]);

  // Handle system theme changes
  useEffect(() => {
    if (theme !== 'system') return;

    const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    const handler = (e: MediaQueryListEvent) => {
      document.documentElement.classList.toggle('dark', e.matches);
    };

    mediaQuery.addEventListener('change', handler);
    return () => mediaQuery.removeEventListener('change', handler);
  }, [theme]);

  // Global keyboard shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Command palette: Cmd+K or Ctrl+K
      if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
        e.preventDefault();
        setCommandPaletteOpen(!commandPaletteOpen);
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [commandPaletteOpen, setCommandPaletteOpen]);

  return (
    <div className="min-h-screen bg-surface-50 dark:bg-surface-950">
      {/* Sidebar */}
      <Sidebar />

      {/* Main Content Area */}
      <div
        className={clsx(
          'main-content',
          sidebarCollapsed && 'main-content-expanded'
        )}
      >
        {/* Header */}
        <Header />

        {/* Page Content */}
        <main className="page">
          <Outlet />
        </main>
      </div>

      {/* Command Palette */}
      <CommandPalette
        isOpen={commandPaletteOpen}
        onClose={() => setCommandPaletteOpen(false)}
      />

      {/* Toast Notifications */}
      <Toaster
        position="bottom-right"
        toastOptions={{
          duration: 4000,
          style: {
            background: 'var(--color-surface-900)',
            color: 'white',
            borderRadius: '12px',
            padding: '12px 16px',
          },
          success: {
            iconTheme: {
              primary: '#22c55e',
              secondary: 'white',
            },
          },
          error: {
            iconTheme: {
              primary: '#ef4444',
              secondary: 'white',
            },
          },
        }}
      />
    </div>
  );
};

export default Layout;
