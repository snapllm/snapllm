// ============================================================================
// SnapLLM - Team Management
// User Roles, Permissions, and Organization Settings
// ============================================================================

import React, { useState } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { clsx } from 'clsx';
import {
  Users,
  UserPlus,
  Mail,
  Shield,
  Settings2,
  MoreVertical,
  Crown,
  UserCheck,
  UserX,
  Key,
  Activity,
  Calendar,
  Search,
  Filter,
  Download,
  Building2,
  Globe,
  Trash2,
  Edit3,
  CheckCircle2,
  Clock,
  AlertCircle,
} from 'lucide-react';
import { Button, IconButton, Badge, Card, Avatar, Modal, Toggle } from '../components/ui';

// ============================================================================
// Types
// ============================================================================

interface TeamMember {
  id: string;
  name: string;
  email: string;
  avatar?: string;
  role: 'owner' | 'admin' | 'member' | 'viewer';
  status: 'active' | 'pending' | 'inactive';
  joinedAt: Date;
  lastActive?: Date;
  apiUsage: number;
}

// ============================================================================
// Sample Data
// ============================================================================

const SAMPLE_MEMBERS: TeamMember[] = [
  { id: '1', name: 'John Smith', email: 'john@company.com', role: 'owner', status: 'active', joinedAt: new Date('2024-01-01'), lastActive: new Date(), apiUsage: 15420 },
  { id: '2', name: 'Sarah Johnson', email: 'sarah@company.com', role: 'admin', status: 'active', joinedAt: new Date('2024-01-15'), lastActive: new Date(), apiUsage: 8750 },
  { id: '3', name: 'Mike Wilson', email: 'mike@company.com', role: 'member', status: 'active', joinedAt: new Date('2024-02-01'), lastActive: new Date('2024-03-10'), apiUsage: 3200 },
  { id: '4', name: 'Emily Davis', email: 'emily@company.com', role: 'member', status: 'pending', joinedAt: new Date('2024-03-01'), apiUsage: 0 },
  { id: '5', name: 'Alex Brown', email: 'alex@company.com', role: 'viewer', status: 'active', joinedAt: new Date('2024-02-15'), lastActive: new Date('2024-03-08'), apiUsage: 450 },
];

const ROLES = [
  { id: 'owner', name: 'Owner', description: 'Full access to all features', color: 'warning' },
  { id: 'admin', name: 'Admin', description: 'Manage team and settings', color: 'brand' },
  { id: 'member', name: 'Member', description: 'Use AI features', color: 'success' },
  { id: 'viewer', name: 'Viewer', description: 'View only access', color: 'default' },
];

// ============================================================================
// Main Component
// ============================================================================

export default function Team() {
  const [members, setMembers] = useState<TeamMember[]>(SAMPLE_MEMBERS);
  const [showInviteModal, setShowInviteModal] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [filterRole, setFilterRole] = useState<string>('all');

  const filteredMembers = members.filter(m => {
    const matchesSearch = m.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
                         m.email.toLowerCase().includes(searchQuery.toLowerCase());
    const matchesRole = filterRole === 'all' || m.role === filterRole;
    return matchesSearch && matchesRole;
  });

  const getRoleColor = (role: TeamMember['role']) => {
    const r = ROLES.find(r => r.id === role);
    return r?.color || 'default';
  };

  const getStatusColor = (status: TeamMember['status']) => {
    switch (status) {
      case 'active': return 'success';
      case 'pending': return 'warning';
      case 'inactive': return 'default';
    }
  };

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold text-surface-900 dark:text-white">
            Team Management
          </h1>
          <p className="text-surface-500 mt-1">
            Manage team members, roles, and permissions
          </p>
        </div>
        <Button variant="primary" onClick={() => setShowInviteModal(true)}>
          <UserPlus className="w-4 h-4" />
          Invite Member
        </Button>
      </div>

      {/* Stats */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
              <Users className="w-5 h-5 text-brand-600 dark:text-brand-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">{members.length}</p>
              <p className="text-sm text-surface-500">Total Members</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
              <UserCheck className="w-5 h-5 text-success-600 dark:text-success-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {members.filter(m => m.status === 'active').length}
              </p>
              <p className="text-sm text-surface-500">Active</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
              <Clock className="w-5 h-5 text-warning-600 dark:text-warning-400" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {members.filter(m => m.status === 'pending').length}
              </p>
              <p className="text-sm text-surface-500">Pending</p>
            </div>
          </div>
        </Card>
        <Card className="p-4">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-lg bg-ai-purple/20 flex items-center justify-center">
              <Activity className="w-5 h-5 text-ai-purple" />
            </div>
            <div>
              <p className="text-2xl font-bold text-surface-900 dark:text-white">
                {members.reduce((sum, m) => sum + m.apiUsage, 0).toLocaleString()}
              </p>
              <p className="text-sm text-surface-500">Total API Calls</p>
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
              placeholder="Search members..."
              className="w-full pl-10 pr-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
            />
          </div>
          <select
            value={filterRole}
            onChange={(e) => setFilterRole(e.target.value)}
            className="px-4 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
          >
            <option value="all">All Roles</option>
            {ROLES.map(role => (
              <option key={role.id} value={role.id}>{role.name}</option>
            ))}
          </select>
          <Button variant="secondary" size="sm">
            <Download className="w-4 h-4 mr-1.5" />
            Export
          </Button>
        </div>
      </Card>

      {/* Members List */}
      <Card className="overflow-hidden">
        <div className="px-6 py-4 border-b border-surface-200 dark:border-surface-700">
          <h2 className="font-semibold text-surface-900 dark:text-white">Team Members</h2>
        </div>
        <div className="divide-y divide-surface-200 dark:divide-surface-700">
          {filteredMembers.map(member => (
            <div
              key={member.id}
              className="p-4 hover:bg-surface-50 dark:hover:bg-surface-800/50 transition-colors"
            >
              <div className="flex items-center gap-4">
                <Avatar name={member.name} src={member.avatar} size="md" />

                <div className="flex-1 min-w-0">
                  <div className="flex items-center gap-2 mb-1">
                    <p className="font-medium text-surface-900 dark:text-white">
                      {member.name}
                    </p>
                    {member.role === 'owner' && (
                      <Crown className="w-4 h-4 text-warning-500" />
                    )}
                    <Badge variant={getStatusColor(member.status) as any} size="sm" dot>
                      {member.status}
                    </Badge>
                  </div>
                  <div className="flex items-center gap-4 text-sm text-surface-500">
                    <span className="flex items-center gap-1">
                      <Mail className="w-3 h-3" />
                      {member.email}
                    </span>
                    <span className="flex items-center gap-1">
                      <Calendar className="w-3 h-3" />
                      Joined {member.joinedAt.toLocaleDateString()}
                    </span>
                  </div>
                </div>

                <div className="flex items-center gap-6">
                  <div>
                    <Badge variant={getRoleColor(member.role) as any}>
                      {member.role.charAt(0).toUpperCase() + member.role.slice(1)}
                    </Badge>
                  </div>
                  <div className="text-right">
                    <p className="text-sm font-medium text-surface-900 dark:text-white">
                      {member.apiUsage.toLocaleString()}
                    </p>
                    <p className="text-xs text-surface-500">API calls</p>
                  </div>
                  {member.lastActive && (
                    <div className="text-right">
                      <p className="text-sm text-surface-700 dark:text-surface-300">
                        {member.lastActive.toLocaleDateString()}
                      </p>
                      <p className="text-xs text-surface-500">Last active</p>
                    </div>
                  )}
                </div>

                <div className="flex items-center gap-1">
                  <IconButton icon={<Edit3 className="w-4 h-4" />} label="Edit" />
                  <IconButton icon={<Key className="w-4 h-4" />} label="Keys" />
                  {member.role !== 'owner' && (
                    <IconButton icon={<Trash2 className="w-4 h-4 text-error-500" />} label="Remove" />
                  )}
                </div>
              </div>
            </div>
          ))}
        </div>
      </Card>

      {/* Roles Reference */}
      <Card className="p-6">
        <h2 className="font-semibold text-surface-900 dark:text-white mb-4">Role Permissions</h2>
        <div className="grid grid-cols-4 gap-4">
          {ROLES.map(role => (
            <div
              key={role.id}
              className="p-4 rounded-xl border border-surface-200 dark:border-surface-700"
            >
              <div className="flex items-center gap-2 mb-2">
                <Badge variant={role.color as any}>{role.name}</Badge>
                {role.id === 'owner' && <Crown className="w-4 h-4 text-warning-500" />}
              </div>
              <p className="text-sm text-surface-500">{role.description}</p>
            </div>
          ))}
        </div>
      </Card>

      {/* Invite Modal */}
      <AnimatePresence>
        {showInviteModal && (
          <Modal
            isOpen={showInviteModal}
            onClose={() => setShowInviteModal(false)}
            title="Invite Team Member"
            size="md"
          >
            <div className="space-y-4">
              <div>
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                  Email Address
                </label>
                <input
                  type="email"
                  placeholder="colleague@company.com"
                  className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500"
                />
              </div>

              <div>
                <label className="text-sm font-medium text-surface-700 dark:text-surface-300 mb-2 block">
                  Role
                </label>
                <select className="w-full px-3 py-2 rounded-lg border border-surface-200 dark:border-surface-700 bg-white dark:bg-surface-800 focus:outline-none focus:ring-2 focus:ring-brand-500">
                  <option value="member">Member</option>
                  <option value="admin">Admin</option>
                  <option value="viewer">Viewer</option>
                </select>
              </div>

              <div className="flex justify-end gap-2 pt-4">
                <Button variant="secondary" onClick={() => setShowInviteModal(false)}>
                  Cancel
                </Button>
                <Button variant="primary">
                  <Mail className="w-4 h-4 mr-1.5" />
                  Send Invite
                </Button>
              </div>
            </div>
          </Modal>
        )}
      </AnimatePresence>
    </div>
  );
}
