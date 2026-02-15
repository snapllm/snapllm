// ============================================================================
// SnapLLM - API Keys Management
// Secure API Key Generation and Management
// ============================================================================

import React, { useState } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Key,
  Plus,
  Copy,
  Check,
  Trash2,
  Eye,
  EyeOff,
  RefreshCw,
  Clock,
  Activity,
  Shield,
  AlertTriangle,
  Settings2,
  MoreVertical,
  Calendar,
  Zap,
  Lock,
  Unlock,
  CheckCircle2,
  XCircle,
} from 'lucide-react';
import { Button, IconButton, Badge, Card, Modal, Toggle } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface ApiKey {
  id: string;
  name: string;
  prefix: string;
  createdAt: Date;
  lastUsed?: Date;
  expiresAt?: Date;
  status: 'active' | 'revoked' | 'expired';
  permissions: string[];
  usageCount: number;
  rateLimit: number;
}

// ============================================================================
// Sample Data
// ============================================================================

const SAMPLE_KEYS: ApiKey[] = [
  {
    id: '1',
    name: 'Production API Key',
    prefix: 'sk-prod-xxxx',
    createdAt: new Date('2024-01-15'),
    lastUsed: new Date(),
    status: 'active',
    permissions: ['chat', 'images', 'embeddings', 'models'],
    usageCount: 15420,
    rateLimit: 1000,
  },
  {
    id: '2',
    name: 'Development Key',
    prefix: 'sk-dev-xxxx',
    createdAt: new Date('2024-02-01'),
    lastUsed: new Date('2024-03-10'),
    status: 'active',
    permissions: ['chat', 'images'],
    usageCount: 3250,
    rateLimit: 100,
  },
  {
    id: '3',
    name: 'Testing Key',
    prefix: 'sk-test-xxxx',
    createdAt: new Date('2024-03-01'),
    expiresAt: new Date('2024-04-01'),
    status: 'active',
    permissions: ['chat'],
    usageCount: 89,
    rateLimit: 50,
  },
];

const PERMISSIONS = [
  { id: 'chat', name: 'Chat Completions', description: 'Generate chat responses' },
  { id: 'images', name: 'Image Generation', description: 'Generate and edit images' },
  { id: 'embeddings', name: 'Embeddings', description: 'Create text embeddings' },
  { id: 'models', name: 'Model Management', description: 'Load and manage models' },
  { id: 'audio', name: 'Audio', description: 'Speech and transcription' },
  { id: 'vision', name: 'Vision', description: 'Image analysis' },
];

// ============================================================================
// Main Component
// ============================================================================

export default function ApiKeys() {
  const [keys, setKeys] = useState<ApiKey[]>(SAMPLE_KEYS);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [newKeyRevealed, setNewKeyRevealed] = useState<string | null>(null);
  const [copiedId, setCopiedId] = useState<string | null>(null);

  const [newKeyForm, setNewKeyForm] = useState({
    name: '',
    permissions: ['chat'],
    rateLimit: 100,
    expiresIn: 'never',
  });

  // Copy key
  const copyKey = (key: ApiKey) => {
    navigator.clipboard.writeText(`${key.prefix}...`);
    setCopiedId(key.id);
    setTimeout(() => setCopiedId(null), 2000);
  };

  // Create new key
  const createKey = () => {
    const newKey: ApiKey = {
      id: crypto.randomUUID(),
      name: newKeyForm.name,
      prefix: `sk-${Math.random().toString(36).substring(2, 8)}`,
      createdAt: new Date(),
      status: 'active',
      permissions: newKeyForm.permissions,
      usageCount: 0,
      rateLimit: newKeyForm.rateLimit,
    };

    setKeys(prev => [newKey, ...prev]);
    setNewKeyRevealed(`sk-${newKey.prefix}-${crypto.randomUUID().replace(/-/g, '')}`);
    setShowCreateModal(false);
    setNewKeyForm({ name: '', permissions: ['chat'], rateLimit: 100, expiresIn: 'never' });
  };

  // Revoke key
  const revokeKey = (keyId: string) => {
    setKeys(prev => prev.map(k =>
      k.id === keyId ? { ...k, status: 'revoked' } : k
    ));
  };

  const getStatusColor = (status: ApiKey['status']) => {
    switch (status) {
      case 'active': return 'success';
      case 'revoked': return 'error';
      case 'expired': return 'warning';
    }
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            API Keys
          </h1>
          <p className="text-surface-500 mt-1">
            Manage API keys for accessing SnapLLM services
          </p>
        </div>
        <Button variant="primary" onClick={() => setShowCreateModal(true)}>
          <Plus className="w-4 h-4" />
          Create API Key
        </Button>
      </div>

      {/* New Key Revealed */}
      <AnimatePresence>
        {newKeyRevealed && (
          <motion.div
            initial={{ opacity: 0, y: -20 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -20 }}
          >
            <Card className="p-4 bg-success-50 dark:bg-success-900/20 border-success-200 dark:border-success-800">
              <div className="flex items-start gap-3">
                <CheckCircle2 className="w-5 h-5 text-success-600 dark:text-success-400 mt-0.5" />
                <div className="flex-1">
                  <p className="font-medium text-success-800 dark:text-success-200 mb-2">
                    API Key Created Successfully
                  </p>
                  <p className="text-sm text-success-700 dark:text-success-300 mb-3">
                    Make sure to copy your API key now. You won't be able to see it again!
                  </p>
                  <div className="flex items-center gap-2">
                    <code className="flex-1 px-3 py-2 rounded-lg bg-success-100 dark:bg-success-900/40 font-mono text-sm text-success-900 dark:text-success-100">
                      {newKeyRevealed}
                    </code>
                    <Button
                      variant="success"
                      size="sm"
                      onClick={() => {
                        navigator.clipboard.writeText(newKeyRevealed);
                        setCopiedId('new');
                        setTimeout(() => setCopiedId(null), 2000);
                      }}
                    >
                      {copiedId === 'new' ? <Check className="w-4 h-4" /> : <Copy className="w-4 h-4" />}
                    </Button>
                  </div>
                </div>
                <button
                  onClick={() => setNewKeyRevealed(null)}
                  className="text-success-600 hover:text-success-800"
                >
                  <XCircle className="w-5 h-5" />
                </button>
              </div>
            </Card>
          </motion.div>
        )}
      </AnimatePresence>

      {/* Stats */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Key className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {keys.filter(k => k.status === 'active').length}
              </p>
              <p className="text-sm text-surface-500">Active Keys</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <Activity className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {keys.reduce((sum, k) => sum + k.usageCount, 0).toLocaleString()}
              </p>
              <p className="text-sm text-surface-500">Total Requests</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Zap className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {Math.max(...keys.map(k => k.rateLimit))}
              </p>
              <p className="text-sm text-surface-500">Max Rate Limit</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-error-100 dark:bg-error-900/30 flex items-center justify-center">
              <Shield className="w-5 h-5 text-error-600 dark:text-error-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {keys.filter(k => k.status === 'revoked').length}
              </p>
              <p className="text-sm text-surface-500">Revoked</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Keys List */}
      <Card className="overflow-hidden">
        <div className="px-6 py-4 border-b border-surface-200 dark:border-surface-700">
          <h2 className="font-semibold text-surface-900 dark:text-white">Your API Keys</h2>
        </div>
        <div className="divide-y divide-surface-200 dark:divide-surface-700">
          {keys.map(key => (
            <div
              key={key.id}
              className={clsx(
                'p-4 hover:bg-surface-50 dark:hover:bg-surface-800/50 transition-colors',
                key.status === 'revoked' && 'opacity-60'
              )}
            >
              <div className="flex items-center gap-4">
                <div className="w-10 h-10 rounded-lg bg-surface-100 dark:bg-surface-800 flex items-center justify-center">
                  <Key className="w-5 h-5 text-surface-500" />
                </div>

                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2 mb-1">
                    <p className="font-medium text-surface-900 dark:text-white">
                      {key.name}
                    </p>
                    <Badge variant={getStatusColor(key.status)} size="sm">
                      {key.status}
                    </Badge>
                  </div>
                  <div className="flex items-center gap-4 text-sm text-surface-500">
                    <code className="font-mono">{key.prefix}...</code>
                    <span className="flex items-center gap-1">
                      <Calendar className="w-3 h-3" />
                      Created {key.createdAt.toLocaleDateString()}
                    </span>
                    {key.lastUsed && (
                      <span className="flex items-center gap-1">
                        <Clock className="w-3 h-3" />
                        Last used {key.lastUsed.toLocaleDateString()}
                      </span>
                    )}
                  </div>
                </div>

                <div className="flex items-center gap-6">
                  <div className="text-right">
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {key.usageCount.toLocaleString()}
                    </p>
                    <p className="text-xs text-surface-500">requests</p>
                  </div>
                  <div className="text-right">
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {key.rateLimit}/min
                    </p>
                    <p className="text-xs text-surface-500">rate limit</p>
                  </div>
                </div>

                <div className="flex items-center gap-1">
                  <IconButton
                    icon={copiedId === key.id ? <Check className="w-4 h-4 text-success-500" /> : <Copy className="w-4 h-4" />}
                    label="Copy"
                    onClick={() => copyKey(key)}
                  />
                  <IconButton icon={<Settings2 className="w-4 h-4" />} label="Settings" />
                  {key.status === 'active' && (
                    <IconButton
                      icon={<Trash2 className="w-4 h-4 text-error-500" />}
                      label="Revoke"
                      onClick={() => revokeKey(key.id)}
                    />
                  )}
                </div>
              </div>

              {/* Permissions */}
              <div className="mt-3 flex items-center gap-2">
                <span className="text-xs text-surface-500">Permissions:</span>
                {key.permissions.map(perm => (
                  <Badge key={perm} variant="default" size="sm">
                    {perm}
                  </Badge>
                ))}
              </div>
            </div>
          ))}
        </div>
      </Card>

      {/* Create Modal */}
      <AnimatePresence>
        {showCreateModal && (
          <Modal
            isOpen={showCreateModal}
            onClose={() => setShowCreateModal(false)}
            title="Create API Key"
            size="md"
          >
            <div className="space-y-4">
              <div>
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                  Key Name
                </label>
                <input
                  type="text"
                  value={newKeyForm.name}
                  onChange={(e) => setNewKeyForm(f => ({ ...f, name: e.target.value }))}
                  placeholder="e.g., Production API Key"
                  className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
                />
              </div>

              <div>
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                  Permissions
                </label>
                <div className="space-y-2">
                  {PERMISSIONS.map(perm => (
                    <label key={perm.id} className="flex items-center gap-3 p-2 rounded-lg hover:bg-surface-50 dark:hover:bg-surface-800 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={newKeyForm.permissions.includes(perm.id)}
                        onChange={(e) => {
                          if (e.target.checked) {
                            setNewKeyForm(f => ({ ...f, permissions: [...f.permissions, perm.id] }));
                          } else {
                            setNewKeyForm(f => ({ ...f, permissions: f.permissions.filter(p => p !== perm.id) }));
                          }
                        }}
                        className="rounded border-surface-300 text-brand-600 focus:ring-brand-500"
                      />
                      <div>
                        <p className="text-sm font-medium text-surface-900 dark:text-white">{perm.name}</p>
                        <p className="text-xs text-surface-500">{perm.description}</p>
                      </div>
                    </label>
                  ))}
                </div>
              </div>

              <div>
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                  Rate Limit (requests/minute)
                </label>
                <input
                  type="number"
                  value={newKeyForm.rateLimit}
                  onChange={(e) => setNewKeyForm(f => ({ ...f, rateLimit: parseInt(e.target.value) || 0 }))}
                  className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
                />
              </div>

              <div className="flex justify-end gap-2 pt-4">
                <Button variant="secondary" onClick={() => setShowCreateModal(false)}>
                  Cancel
                </Button>
                <Button variant="primary" onClick={createKey} disabled={!newKeyForm.name}>
                  Create Key
                </Button>
              </div>
            </div>
          </Modal>
        )}
      </AnimatePresence>
    </div>
  );
}
