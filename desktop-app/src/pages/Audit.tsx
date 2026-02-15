// ============================================================================
// SnapLLM - Audit Log
// Activity Tracking and Security Monitoring
// ============================================================================

import React, { useState } from 'react';
import { motion } from 'framer-motion';
import { clsx } from 'clsx';
import {
  FileText,
  Search,
  Filter,
  Download,
  Calendar,
  Clock,
  User,
  Key,
  Shield,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  Info,
  Activity,
  Cpu,
  Database,
  Globe,
  Settings,
  LogIn,
  LogOut,
  Plus,
  Trash2,
  Edit3,
  Eye,
  RefreshCw,
} from 'lucide-react';
import { Button, IconButton, Badge, Card } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface AuditEvent {
  id: string;
  timestamp: Date;
  action: string;
  category: 'auth' | 'api' | 'model' | 'config' | 'security' | 'user';
  actor: { name: string; email: string };
  resource?: string;
  status: 'success' | 'warning' | 'error';
  details?: string;
  ip?: string;
}

// ============================================================================
// Sample Data
// ============================================================================

const SAMPLE_EVENTS: AuditEvent[] = [
  { id: '1', timestamp: new Date(), action: 'model.load', category: 'model', actor: { name: 'John Smith', email: 'john@company.com' }, resource: 'llama-3-8b', status: 'success', details: 'Model loaded successfully', ip: '192.168.1.100' },
  { id: '2', timestamp: new Date(Date.now() - 300000), action: 'api.request', category: 'api', actor: { name: 'API Key', email: 'sk-prod-xxxx' }, resource: '/v1/chat/completions', status: 'success', ip: '10.0.0.1' },
  { id: '3', timestamp: new Date(Date.now() - 600000), action: 'user.login', category: 'auth', actor: { name: 'Sarah Johnson', email: 'sarah@company.com' }, status: 'success', ip: '192.168.1.101' },
  { id: '4', timestamp: new Date(Date.now() - 900000), action: 'api.rate_limit', category: 'security', actor: { name: 'API Key', email: 'sk-test-xxxx' }, status: 'warning', details: 'Rate limit exceeded', ip: '203.0.113.50' },
  { id: '5', timestamp: new Date(Date.now() - 1200000), action: 'config.update', category: 'config', actor: { name: 'John Smith', email: 'john@company.com' }, resource: 'generation.max_tokens', status: 'success', details: 'Changed from 1024 to 2048' },
  { id: '6', timestamp: new Date(Date.now() - 1500000), action: 'model.switch', category: 'model', actor: { name: 'System', email: 'system' }, resource: 'gemma-7b', status: 'success', details: 'vPID switch in 0.8ms' },
  { id: '7', timestamp: new Date(Date.now() - 1800000), action: 'auth.failed', category: 'security', actor: { name: 'Unknown', email: 'unknown' }, status: 'error', details: 'Invalid API key', ip: '45.33.32.156' },
  { id: '8', timestamp: new Date(Date.now() - 2100000), action: 'user.invite', category: 'user', actor: { name: 'John Smith', email: 'john@company.com' }, resource: 'mike@company.com', status: 'success' },
];

const CATEGORIES = [
  { id: 'all', name: 'All Events', icon: Activity },
  { id: 'auth', name: 'Authentication', icon: LogIn },
  { id: 'api', name: 'API Requests', icon: Globe },
  { id: 'model', name: 'Model Operations', icon: Cpu },
  { id: 'config', name: 'Configuration', icon: Settings },
  { id: 'security', name: 'Security', icon: Shield },
  { id: 'user', name: 'User Management', icon: User },
];

// ============================================================================
// Main Component
// ============================================================================

export default function Audit() {
  const [events] = useState<AuditEvent[]>(SAMPLE_EVENTS);
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedCategory, setSelectedCategory] = useState('all');
  const [dateRange, setDateRange] = useState('today');

  const filteredEvents = events.filter(e => {
    const matchesSearch = e.action.toLowerCase().includes(searchQuery.toLowerCase()) ||
                         e.actor.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
                         e.resource?.toLowerCase().includes(searchQuery.toLowerCase());
    const matchesCategory = selectedCategory === 'all' || e.category === selectedCategory;
    return matchesSearch && matchesCategory;
  });

  const getStatusIcon = (status: AuditEvent['status']) => {
    switch (status) {
      case 'success': return <CheckCircle2 className="w-4 h-4 text-success-500" />;
      case 'warning': return <AlertTriangle className="w-4 h-4 text-warning-500" />;
      case 'error': return <XCircle className="w-4 h-4 text-error-500" />;
    }
  };

  const getCategoryIcon = (category: AuditEvent['category']) => {
    const cat = CATEGORIES.find(c => c.id === category);
    return cat ? cat.icon : Activity;
  };

  const getActionLabel = (action: string) => {
    const parts = action.split('.');
    return parts.map(p => p.charAt(0).toUpperCase() + p.slice(1)).join(' → ');
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Audit Log
          </h1>
          <p className="text-surface-500 mt-1">
            Track all activity and security events
          </p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="secondary">
            <RefreshCw className="w-4 h-4" />
            Refresh
          </Button>
          <Button variant="secondary">
            <Download className="w-4 h-4" />
            Export
          </Button>
        </div>
      </div>

      {/* Stats */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Activity className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">{events.length}</p>
              <p className="text-sm text-surface-500">Total Events</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <CheckCircle2 className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {events.filter(e => e.status === 'success').length}
              </p>
              <p className="text-sm text-surface-500">Successful</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <AlertTriangle className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {events.filter(e => e.status === 'warning').length}
              </p>
              <p className="text-sm text-surface-500">Warnings</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-error-100 dark:bg-error-900/30 flex items-center justify-center">
              <XCircle className="w-5 h-5 text-error-600 dark:text-error-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {events.filter(e => e.status === 'error').length}
              </p>
              <p className="text-sm text-surface-500">Errors</p>
            </div>
          </div>
        </Card>
      </div>

      {/* Filters */}
      <Card className="p-4">
        <div className="flex items-center gap-4">
          <div className="flex-1 relative">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-surface-400" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Search events..."
              className="w-full pl-10 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
            />
          </div>

          <div className="flex items-center gap-2">
            {CATEGORIES.slice(0, 5).map(cat => (
              <button
                key={cat.id}
                onClick={() => setSelectedCategory(cat.id)}
                className={clsx(
                  'flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium transition-colors',
                  selectedCategory === cat.id
                    ? 'bg-brand-100 dark:bg-brand-900/30 text-brand-700 dark:text-brand-300'
                    : 'text-surface-600 dark:text-surface-400 hover:bg-surface-100 dark:hover:bg-surface-800'
                )}
              >
                <cat.icon className="w-4 h-4" />
                {cat.name}
              </button>
            ))}
          </div>

          <select
            value={dateRange}
            onChange={(e) => setDateRange(e.target.value)}
            className="px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
          >
            <option value="today">Today</option>
            <option value="week">This Week</option>
            <option value="month">This Month</option>
            <option value="all">All Time</option>
          </select>
        </div>
      </Card>

      {/* Events List */}
      <Card className="overflow-hidden">
        <div className="px-6 py-4 border-b border-surface-200 dark:border-surface-700">
          <h2 className="font-semibold text-surface-900 dark:text-white">
            Events ({filteredEvents.length})
          </h2>
        </div>
        <div className="divide-y divide-surface-200 dark:divide-surface-700">
          {filteredEvents.map((event, i) => {
            const CategoryIcon = getCategoryIcon(event.category);
            return (
              <motion.div
                key={event.id}
                initial={{ opacity: 0, y: 10 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: i * 0.05 }}
                className="p-4 hover:bg-surface-50 dark:hover:bg-surface-800/50 transition-colors"
              >
                <div className="flex items-start gap-4">
                  {/* Status Icon */}
                  <div className="mt-0.5">
                    {getStatusIcon(event.status)}
                  </div>

                  {/* Event Details */}
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center gap-2 mb-1">
                      <span className="font-medium text-surface-900 dark:text-white">
                        {getActionLabel(event.action)}
                      </span>
                      <Badge variant="default" size="sm">
                        <CategoryIcon className="w-3 h-3" />
                        {event.category}
                      </Badge>
                    </div>

                    <div className="flex items-center gap-4 text-sm text-surface-500">
                      <span className="flex items-center gap-1">
                        <User className="w-3 h-3" />
                        {event.actor.name}
                      </span>
                      {event.resource && (
                        <span className="flex items-center gap-1">
                          <Database className="w-3 h-3" />
                          {event.resource}
                        </span>
                      )}
                      {event.ip && (
                        <span className="flex items-center gap-1">
                          <Globe className="w-3 h-3" />
                          {event.ip}
                        </span>
                      )}
                    </div>

                    {event.details && (
                      <p className="text-sm text-surface-600 dark:text-surface-400 mt-1">
                        {event.details}
                      </p>
                    )}
                  </div>

                  {/* Timestamp */}
                  <div className="text-right text-sm text-surface-500">
                    <div className="flex items-center gap-1">
                      <Clock className="w-3 h-3" />
                      {event.timestamp.toLocaleTimeString()}
                    </div>
                    <div className="text-xs">
                      {event.timestamp.toLocaleDateString()}
                    </div>
                  </div>

                  {/* Actions */}
                  <IconButton icon={<Eye className="w-4 h-4" />} label="View details" />
                </div>
              </motion.div>
            );
          })}
        </div>
      </Card>
    </div>
  );
}
