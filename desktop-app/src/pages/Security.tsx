// ============================================================================
// SnapLLM - Security Settings
// Access Controls, Encryption, and Security Configuration
// ============================================================================

import React, { useState } from 'react';
import { motion } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Shield,
  Lock,
  Unlock,
  Key,
  Eye,
  EyeOff,
  AlertTriangle,
  CheckCircle2,
  XCircle,
  Settings,
  Globe,
  Users,
  Smartphone,
  Mail,
  RefreshCw,
  Clock,
  Activity,
  Fingerprint,
  ShieldCheck,
  ShieldAlert,
  Ban,
  MapPin,
  Monitor,
  Wifi,
  Server,
} from 'lucide-react';
import { Button, IconButton, Badge, Card, Toggle, Progress } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface SecuritySetting {
  id: string;
  name: string;
  description: string;
  enabled: boolean;
  category: 'auth' | 'api' | 'network' | 'data';
}

interface ActiveSession {
  id: string;
  device: string;
  browser: string;
  location: string;
  ip: string;
  lastActive: Date;
  current: boolean;
}

// ============================================================================
// Sample Data
// ============================================================================

const SECURITY_SETTINGS: SecuritySetting[] = [
  { id: 'mfa', name: 'Two-Factor Authentication', description: 'Require 2FA for all team members', enabled: true, category: 'auth' },
  { id: 'sso', name: 'Single Sign-On (SSO)', description: 'Enable SAML/OIDC authentication', enabled: false, category: 'auth' },
  { id: 'api-auth', name: 'API Key Authentication', description: 'Require API keys for all requests', enabled: true, category: 'api' },
  { id: 'rate-limit', name: 'Rate Limiting', description: 'Limit API requests per minute', enabled: true, category: 'api' },
  { id: 'ip-whitelist', name: 'IP Whitelist', description: 'Restrict access to specific IPs', enabled: false, category: 'network' },
  { id: 'encryption', name: 'Data Encryption', description: 'Encrypt data at rest and in transit', enabled: true, category: 'data' },
  { id: 'audit-log', name: 'Audit Logging', description: 'Log all API and user activity', enabled: true, category: 'data' },
  { id: 'model-access', name: 'Model Access Control', description: 'Restrict model access by role', enabled: true, category: 'data' },
];

const ACTIVE_SESSIONS: ActiveSession[] = [
  { id: '1', device: 'Windows PC', browser: 'Chrome 122', location: 'San Francisco, CA', ip: '192.168.1.100', lastActive: new Date(), current: true },
  { id: '2', device: 'MacBook Pro', browser: 'Safari 17', location: 'New York, NY', ip: '10.0.0.50', lastActive: new Date(Date.now() - 3600000), current: false },
  { id: '3', device: 'iPhone 15', browser: 'Safari Mobile', location: 'Los Angeles, CA', ip: '172.16.0.1', lastActive: new Date(Date.now() - 86400000), current: false },
];

// ============================================================================
// Main Component
// ============================================================================

export default function Security() {
  const [settings, setSettings] = useState<SecuritySetting[]>(SECURITY_SETTINGS);
  const [sessions] = useState<ActiveSession[]>(ACTIVE_SESSIONS);

  const toggleSetting = (id: string) => {
    setSettings(prev => prev.map(s =>
      s.id === id ? { ...s, enabled: !s.enabled } : s
    ));
  };

  const securityScore = Math.round(
    (settings.filter(s => s.enabled).length / settings.length) * 100
  );

  const getScoreColor = (score: number) => {
    if (score >= 80) return 'success';
    if (score >= 60) return 'warning';
    return 'error';
  };

  const getCategoryIcon = (category: SecuritySetting['category']) => {
    switch (category) {
      case 'auth': return Fingerprint;
      case 'api': return Key;
      case 'network': return Globe;
      case 'data': return Lock;
    }
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Security Settings
          </h1>
          <p className="text-surface-500 mt-1">
            Configure access controls and security policies
          </p>
        </div>
        <Button variant="primary">
          <ShieldCheck className="w-4 h-4" />
          Security Audit
        </Button>
      </div>

      {/* Security Score */}
      <Card className="p-6">
        <div className="flex items-center gap-6">
          <div className={clsx(
            'w-24 h-24 rounded-2xl flex items-center justify-center',
            securityScore >= 80 ? 'bg-success-100 dark:bg-success-900/30' :
            securityScore >= 60 ? 'bg-warning-100 dark:bg-warning-900/30' :
            'bg-error-100 dark:bg-error-900/30'
          )}>
            <div className="text-center">
              <p className={clsx(
                'text-3xl font-bold',
                securityScore >= 80 ? 'text-success-600 dark:text-success-400' :
                securityScore >= 60 ? 'text-warning-600 dark:text-warning-400' :
                'text-error-600 dark:text-error-400'
              )}>
                {securityScore}
              </p>
              <p className="text-xs text-surface-500">Score</p>
            </div>
          </div>

          <div className="flex-1">
            <h3 className="text-lg font-semibold text-surface-900 dark:text-white mb-2">
              Security Score
            </h3>
            <Progress value={securityScore} variant={getScoreColor(securityScore)} className="mb-2" />
            <p className="text-sm text-surface-500">
              {securityScore >= 80 ? 'Your security configuration is excellent.' :
               securityScore >= 60 ? 'Consider enabling more security features.' :
               'Your security configuration needs attention.'}
            </p>
          </div>

          <div className="flex flex-col gap-2">
            <div className="flex items-center gap-2">
              <CheckCircle2 className="w-4 h-4 text-success-500" />
              <span className="text-sm text-surface-600 dark:text-surface-400">
                {settings.filter(s => s.enabled).length} enabled
              </span>
            </div>
            <div className="flex items-center gap-2">
              <XCircle className="w-4 h-4 text-error-500" />
              <span className="text-sm text-surface-600 dark:text-surface-400">
                {settings.filter(s => !s.enabled).length} disabled
              </span>
            </div>
          </div>
        </div>
      </Card>

      <div className="grid grid-cols-3 gap-6">
        {/* Security Settings */}
        <div className="col-span-2 space-y-4">
          {(['auth', 'api', 'network', 'data'] as const).map(category => {
            const categorySettings = settings.filter(s => s.category === category);
            const CategoryIcon = getCategoryIcon(category);

            return (
              <Card key={category} className="p-6">
                <div className="flex items-center gap-3 mb-4">
                  <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
                    <CategoryIcon className="w-5 h-5 text-brand-600 dark:text-brand-400" />
                  </div>
                  <div>
                    <h3 className="font-semibold text-surface-900 dark:text-white capitalize">
                      {category === 'auth' ? 'Authentication' :
                       category === 'api' ? 'API Security' :
                       category === 'network' ? 'Network' : 'Data Protection'}
                    </h3>
                    <p className="text-sm text-surface-500">
                      {categorySettings.filter(s => s.enabled).length} of {categorySettings.length} enabled
                    </p>
                  </div>
                </div>

                <div className="space-y-3">
                  {categorySettings.map(setting => (
                    <div
                      key={setting.id}
                      className="flex items-center justify-between p-3 rounded-lg bg-surface-50 dark:bg-surface-800"
                    >
                      <div className="flex-1">
                        <div className="flex items-center gap-2">
                          <p className="font-medium text-surface-900 dark:text-white">
                            {setting.name}
                          </p>
                          {setting.enabled ? (
                            <CheckCircle2 className="w-4 h-4 text-success-500" />
                          ) : (
                            <AlertTriangle className="w-4 h-4 text-warning-500" />
                          )}
                        </div>
                        <p className="text-sm text-surface-500">{setting.description}</p>
                      </div>
                      <Toggle
                        checked={setting.enabled}
                        onChange={() => toggleSetting(setting.id)}
                      />
                    </div>
                  ))}
                </div>
              </Card>
            );
          })}
        </div>

        {/* Active Sessions */}
        <div className="space-y-4">
          <Card className="p-6">
            <h3 className="font-semibold text-surface-900 dark:text-white mb-4">
              Active Sessions
            </h3>
            <div className="space-y-3">
              {sessions.map(session => (
                <div
                  key={session.id}
                  className={clsx(
                    'p-3 rounded-lg border',
                    session.current
                      ? 'border-success-200 dark:border-success-800 bg-success-50 dark:bg-success-900/20'
                      : 'border-surface-200 dark:border-surface-700'
                  )}
                >
                  <div className="flex items-start justify-between mb-2">
                    <div className="flex items-center gap-2">
                      <Monitor className="w-4 h-4 text-surface-500" />
                      <span className="text-sm font-medium text-surface-900 dark:text-white">
                        {session.device}
                      </span>
                      {session.current && (
                        <Badge variant="success" size="sm">Current</Badge>
                      )}
                    </div>
                    {!session.current && (
                      <button className="text-error-500 hover:text-error-600">
                        <Ban className="w-4 h-4" />
                      </button>
                    )}
                  </div>
                  <div className="space-y-1 text-xs text-surface-500">
                    <div className="flex items-center gap-1">
                      <Globe className="w-3 h-3" />
                      {session.browser}
                    </div>
                    <div className="flex items-center gap-1">
                      <MapPin className="w-3 h-3" />
                      {session.location}
                    </div>
                    <div className="flex items-center gap-1">
                      <Wifi className="w-3 h-3" />
                      {session.ip}
                    </div>
                    <div className="flex items-center gap-1">
                      <Clock className="w-3 h-3" />
                      {session.lastActive.toLocaleString()}
                    </div>
                  </div>
                </div>
              ))}
            </div>
            <Button variant="ghost" size="sm" className="w-full mt-3">
              <RefreshCw className="w-4 h-4 mr-1.5" />
              Revoke All Other Sessions
            </Button>
          </Card>

          {/* Quick Actions */}
          <Card className="p-6">
            <h3 className="font-semibold text-surface-900 dark:text-white mb-4">
              Quick Actions
            </h3>
            <div className="space-y-2">
              <Button variant="secondary" className="w-full justify-start">
                <Key className="w-4 h-4" />
                Rotate API Keys
              </Button>
              <Button variant="secondary" className="w-full justify-start">
                <RefreshCw className="w-4 h-4" />
                Force Password Reset
              </Button>
              <Button variant="secondary" className="w-full justify-start">
                <ShieldAlert className="w-4 h-4" />
                View Threat Report
              </Button>
            </div>
          </Card>
        </div>
      </div>
    </div>
  );
}
