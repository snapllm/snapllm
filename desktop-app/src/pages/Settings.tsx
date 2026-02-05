import React from 'react';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import {
  AlertTriangle,
  CheckCircle2,
  Cpu,
  FolderOpen,
  HardDrive,
  Info,
  Network,
  Save,
  Server,
  Settings,
  Sparkles,
  RotateCcw,
  XCircle,
} from 'lucide-react';
import {
  ConfigResponse,
  ConfigUpdateRequest,
  getConfig,
  getConfigRecommendations,
  getDefaultModelsPath,
  getDefaultWorkspacePath,
  handleApiError,
  updateConfig,
} from '../lib/api';
import { Alert, Badge, Button, Card, Input, Select, Toggle } from '../components/ui';

const STRATEGY_OPTIONS = [
  { value: 'balanced', label: 'Balanced (default)' },
  { value: 'conservative', label: 'Conservative' },
  { value: 'aggressive', label: 'Aggressive' },
  { value: 'performance', label: 'Performance' },
];

type SettingsFormState = {
  host: string;
  port: string;
  workspace_root: string;
  default_models_path: string;
  cors_enabled: boolean;
  timeout_seconds: string;
  max_concurrent_requests: string;
  max_models: string;
  default_ram_budget_mb: string;
  default_strategy: string;
  enable_gpu: boolean;
};

const formatGb = (mb?: number) => {
  if (mb === null || mb === undefined) return 'N/A';
  return `${(mb / 1024).toFixed(1)} GB`;
};

const formatValue = (value?: string | number | boolean) => {
  if (value === null || value === undefined || value === '') return 'N/A';
  if (typeof value === 'boolean') return value ? 'Yes' : 'No';
  return String(value);
};

const isWindows = typeof navigator !== 'undefined' && navigator.userAgent.toLowerCase().includes('windows');

const mapConfigToForm = (config: ConfigResponse): SettingsFormState => {
  const defaultWorkspaceBase = getDefaultWorkspacePath();
  const defaultModelsBase = getDefaultModelsPath();
  const defaultWorkspace = isWindows ? defaultWorkspaceBase.replace(/\//g, '\\') : defaultWorkspaceBase;
  const defaultModels = isWindows ? defaultModelsBase.replace(/\//g, '\\') : defaultModelsBase;

  return {
    host: config.host || '127.0.0.1',
    port: String(config.port ?? 6930),
    workspace_root: config.workspace_root || defaultWorkspace,
    default_models_path: config.default_models_path || defaultModels,
    cors_enabled: config.cors_enabled ?? true,
    timeout_seconds: String(config.timeout_seconds ?? 600),
    max_concurrent_requests: String(config.max_concurrent_requests ?? 8),
    max_models: String(config.max_models ?? 10),
    default_ram_budget_mb: String(config.default_ram_budget_mb ?? 16384),
    default_strategy: config.default_strategy || 'balanced',
    enable_gpu: config.enable_gpu ?? true,
  };
};

const parseInteger = (value: string) => {
  if (value.trim() === '') return null;
  const parsed = Number(value);
  if (!Number.isInteger(parsed)) return null;
  return parsed;
};

type SettingsErrors = Partial<Record<keyof SettingsFormState, string>>;

const validateForm = (values: SettingsFormState): SettingsErrors => {
  const errors: SettingsErrors = {};

  if (!values.host.trim()) {
    errors.host = 'Host is required.';
  }

  const port = parseInteger(values.port);
  if (port === null) {
    errors.port = 'Port must be an integer.';
  } else if (port < 1 || port > 65535) {
    errors.port = 'Port must be between 1 and 65535.';
  }

  if (!values.workspace_root.trim()) {
    errors.workspace_root = 'Workspace root is required.';
  }

  if (!values.default_models_path.trim()) {
    errors.default_models_path = 'Models path is required.';
  }

  const timeout = parseInteger(values.timeout_seconds);
  if (timeout === null) {
    errors.timeout_seconds = 'Timeout must be an integer.';
  } else if (timeout < 30 || timeout > 86400) {
    errors.timeout_seconds = 'Timeout must be between 30 and 86400 seconds.';
  }

  const maxConcurrent = parseInteger(values.max_concurrent_requests);
  if (maxConcurrent === null) {
    errors.max_concurrent_requests = 'Max concurrent requests must be an integer.';
  } else if (maxConcurrent < 1 || maxConcurrent > 128) {
    errors.max_concurrent_requests = 'Max concurrent requests must be between 1 and 128.';
  }

  const maxModels = parseInteger(values.max_models);
  if (maxModels === null) {
    errors.max_models = 'Max models must be an integer.';
  } else if (maxModels < 1 || maxModels > 64) {
    errors.max_models = 'Max models must be between 1 and 64.';
  }

  const ramBudget = parseInteger(values.default_ram_budget_mb);
  if (ramBudget === null) {
    errors.default_ram_budget_mb = 'RAM budget must be an integer.';
  } else if (ramBudget < 512 || ramBudget > 1048576) {
    errors.default_ram_budget_mb = 'RAM budget must be between 512 and 1048576 MB.';
  }

  if (!STRATEGY_OPTIONS.some((option) => option.value === values.default_strategy)) {
    errors.default_strategy = 'Select a valid default strategy.';
  }

  return errors;
};

export default function SettingsPage() {
  const queryClient = useQueryClient();
  const { data: config } = useQuery({
    queryKey: ['config'],
    queryFn: getConfig,
  });

  const { data: recommendations } = useQuery({
    queryKey: ['recommendations'],
    queryFn: getConfigRecommendations,
  });

  const [formState, setFormState] = React.useState<SettingsFormState | null>(null);
  const [baseState, setBaseState] = React.useState<SettingsFormState | null>(null);
  const [errors, setErrors] = React.useState<SettingsErrors>({});
  const [saveMessage, setSaveMessage] = React.useState<string | null>(null);
  const [saveError, setSaveError] = React.useState<string | null>(null);
  const [restartFields, setRestartFields] = React.useState<string[]>([]);

  const isConnected = config?.status === 'success';
  const features = config?.features || {};

  const updateMutation = useMutation({
    mutationFn: (payload: ConfigUpdateRequest) => updateConfig(payload),
    onSuccess: (data) => {
      setSaveError(null);
      setSaveMessage(`Settings saved to ${data.config_path || 'config.json'}.`);
      setRestartFields(data.restart_required_fields || []);
      if (formState) {
        setBaseState(formState);
      }
      queryClient.invalidateQueries({ queryKey: ['config'] });
    },
    onError: (error) => {
      setSaveMessage(null);
      setRestartFields([]);
      setSaveError(handleApiError(error));
    },
  });

  const isDirty = React.useMemo(() => {
    if (!formState || !baseState) return false;
    return JSON.stringify(formState) !== JSON.stringify(baseState);
  }, [formState, baseState]);

  const changedFields = React.useMemo(() => {
    if (!formState || !baseState) return [] as string[];
    return Object.keys(formState).filter((key) => {
      const typedKey = key as keyof SettingsFormState;
      return formState[typedKey] !== baseState[typedKey];
    });
  }, [formState, baseState]);

  React.useEffect(() => {
    if (!config) return;
    const nextForm = mapConfigToForm(config);
    const nextSerialized = JSON.stringify(nextForm);
    const baseSerialized = baseState ? JSON.stringify(baseState) : '';
    const shouldSync = !baseState || !formState || (!isDirty && nextSerialized !== baseSerialized);
    if (shouldSync) {
      setFormState(nextForm);
      setBaseState(nextForm);
      setErrors({});
    }
  }, [config, baseState, formState, isDirty]);

  const updateField = <K extends keyof SettingsFormState>(key: K, value: SettingsFormState[K]) => {
    setFormState((prev) => (prev ? { ...prev, [key]: value } : prev));
    if (errors[key]) {
      setErrors((prev) => ({ ...prev, [key]: undefined }));
    }
  };

  const handleDiscard = () => {
    if (baseState) {
      setFormState(baseState);
      setErrors({});
      setSaveError(null);
      setSaveMessage(null);
      setRestartFields([]);
    }
  };

  const handleSave = () => {
    if (!formState) return;
    const validation = validateForm(formState);
    setErrors(validation);
    if (Object.keys(validation).length > 0) {
      setSaveError('Fix the highlighted fields before saving.');
      return;
    }

    setSaveError(null);
    setSaveMessage(null);
    setRestartFields([]);

    const payload: ConfigUpdateRequest = {
      server: {
        host: formState.host.trim(),
        port: Number(formState.port),
        cors_enabled: formState.cors_enabled,
        timeout_seconds: Number(formState.timeout_seconds),
        max_concurrent_requests: Number(formState.max_concurrent_requests),
      },
      workspace: {
        root: formState.workspace_root.trim(),
        default_models_path: formState.default_models_path.trim(),
      },
      runtime: {
        max_models: Number(formState.max_models),
        default_ram_budget_mb: Number(formState.default_ram_budget_mb),
        default_strategy: formState.default_strategy,
        enable_gpu: formState.enable_gpu,
      },
    };

    updateMutation.mutate(payload);
  };

  const featureRows = [
    { key: 'llm', label: 'LLM Inference', description: 'Text and chat generation', enabled: features.llm },
    { key: 'diffusion', label: 'Diffusion', description: 'Image generation', enabled: features.diffusion },
    { key: 'vision', label: 'Vision', description: 'Multimodal understanding', enabled: features.vision },
    { key: 'video', label: 'Video', description: 'Video generation', enabled: features.video },
  ];

  return (
    <div className="p-6 md:p-8 max-w-6xl mx-auto space-y-6">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-start lg:justify-between">
        <div className="flex items-start gap-3">
          <div className="w-11 h-11 rounded-xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
            <Settings className="w-5 h-5 text-white" />
          </div>
          <div>
            <h1 className="text-3xl font-bold text-surface-900 dark:text-white">Server Settings</h1>
            <p className="text-surface-500">
              Edit the local SnapLLM server configuration and persist it to the config file.
            </p>
          </div>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <Badge variant={isConnected ? 'success' : 'warning'}>
            {isConnected ? 'Connected' : 'Server offline'}
          </Badge>
          <Badge variant="default">/api/v1/config</Badge>
          {changedFields.length > 0 && (
            <Badge variant="warning">{changedFields.length} pending</Badge>
          )}
        </div>
      </div>

      <div className="flex flex-col gap-3 md:flex-row md:items-center md:justify-between">
        <div className="text-sm text-surface-600 dark:text-surface-400">
          {config?.config_path ? (
            <span className="inline-flex items-center gap-2">
              <Info className="w-4 h-4" />
              Config file: <span className="font-mono text-surface-900 dark:text-surface-200">{config.config_path}</span>
            </span>
          ) : (
            <span className="inline-flex items-center gap-2">
              <Info className="w-4 h-4" />
              Config file path will be detected once the server reports it.
            </span>
          )}
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <Button
            variant="secondary"
            size="sm"
            leftIcon={<RotateCcw className="w-4 h-4" />}
            onClick={handleDiscard}
            disabled={!isDirty || updateMutation.isPending}
          >
            Discard
          </Button>
          <Button
            variant="primary"
            size="sm"
            leftIcon={<Save className="w-4 h-4" />}
            onClick={handleSave}
            isLoading={updateMutation.isPending}
            disabled={!isDirty || !isConnected}
          >
            Save Changes
          </Button>
        </div>
      </div>

      {!isConnected && (
        <Alert variant="warning" title="Server offline">
          Start the SnapLLM server to apply settings changes. You can still edit values, but saving
          requires an active server instance.
        </Alert>
      )}

      {saveError && (
        <Alert variant="error" title="Save failed">
          {saveError}
        </Alert>
      )}

      {saveMessage && (
        <Alert variant="success" title="Settings saved">
          {saveMessage}
        </Alert>
      )}

      {restartFields.length > 0 && (
        <Alert variant="info" title="Restart required">
          Some changes require a server restart: {restartFields.join(', ')}.
        </Alert>
      )}

      <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
        <div className="xl:col-span-2 space-y-6">
          <Card className="space-y-5">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="text-lg font-semibold text-surface-900 dark:text-white">Connection</h2>
                <p className="text-sm text-surface-500">Network settings for the local API server.</p>
              </div>
              <Badge variant="info">Restart required</Badge>
            </div>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <Input
                label="Host"
                value={formState?.host || ''}
                onChange={(event) => updateField('host', event.target.value)}
                error={errors.host}
                hint="Requires server restart"
                leftIcon={<Network className="w-4 h-4" />}
              />
              <Input
                label="Port"
                type="number"
                value={formState?.port || ''}
                onChange={(event) => updateField('port', event.target.value)}
                error={errors.port}
                hint="Requires server restart"
                leftIcon={<Server className="w-4 h-4" />}
                min={1}
                max={65535}
              />
              <Input
                label="Request Timeout (seconds)"
                type="number"
                value={formState?.timeout_seconds || ''}
                onChange={(event) => updateField('timeout_seconds', event.target.value)}
                error={errors.timeout_seconds}
                hint="Requires server restart"
                min={30}
                max={86400}
              />
              <Input
                label="Max Concurrent Requests"
                type="number"
                value={formState?.max_concurrent_requests || ''}
                onChange={(event) => updateField('max_concurrent_requests', event.target.value)}
                error={errors.max_concurrent_requests}
                hint="Requires server restart"
                min={1}
                max={128}
              />
            </div>
            <Toggle
              checked={formState?.cors_enabled ?? true}
              onChange={(value) => updateField('cors_enabled', value)}
              label="Enable CORS"
              description="Allow browser clients to access the API from other origins. Requires restart."
            />
          </Card>

          <Card className="space-y-5">
            <div>
              <h2 className="text-lg font-semibold text-surface-900 dark:text-white">Workspace & Models</h2>
              <p className="text-sm text-surface-500">Paths used for vPID caches, logs, and model files.</p>
            </div>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <Input
                label="Workspace Root"
                value={formState?.workspace_root || ''}
                onChange={(event) => updateField('workspace_root', event.target.value)}
                error={errors.workspace_root}
                hint="Requires server restart"
                leftIcon={<HardDrive className="w-4 h-4" />}
              />
              <Input
                label="Default Models Path"
                value={formState?.default_models_path || ''}
                onChange={(event) => updateField('default_models_path', event.target.value)}
                error={errors.default_models_path}
                hint="Used for model discovery and uploads"
                leftIcon={<FolderOpen className="w-4 h-4" />}
              />
            </div>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <Input
                label="Max Models"
                type="number"
                value={formState?.max_models || ''}
                onChange={(event) => updateField('max_models', event.target.value)}
                error={errors.max_models}
                hint="UI limit for concurrent models"
                min={1}
                max={64}
              />
              <Input
                label="Default RAM Budget (MB)"
                type="number"
                value={formState?.default_ram_budget_mb || ''}
                onChange={(event) => updateField('default_ram_budget_mb', event.target.value)}
                error={errors.default_ram_budget_mb}
                hint="Helps size model cache allocations"
                min={512}
                max={1048576}
              />
            </div>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <Select
                label="Default Strategy"
                options={STRATEGY_OPTIONS}
                value={formState?.default_strategy || 'balanced'}
                onChange={(event) => updateField('default_strategy', event.target.value)}
                error={errors.default_strategy}
                hint="Used when loading new models"
              />
              <div className="flex items-center">
                <Toggle
                  checked={formState?.enable_gpu ?? true}
                  onChange={(value) => updateField('enable_gpu', value)}
                  label="GPU Enabled"
                  description="Defaults to GPU if available"
                />
              </div>
            </div>
          </Card>

          <Card className="space-y-4">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="text-lg font-semibold text-surface-900 dark:text-white">Recommended Targets</h2>
                <p className="text-sm text-surface-500">Hardware-aware guidance from the server.</p>
              </div>
              <Badge variant="info">Advisory</Badge>
            </div>
            <div className="grid grid-cols-2 gap-4">
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <p className="text-xs text-surface-500">RAM Budget</p>
                <p className="text-lg font-semibold text-surface-900 dark:text-white">
                  {formatGb(recommendations?.recommended_ram_budget_mb)}
                </p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <p className="text-xs text-surface-500">Strategy</p>
                <p className="text-lg font-semibold text-surface-900 dark:text-white capitalize">
                  {formatValue(recommendations?.recommended_strategy)}
                </p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <p className="text-xs text-surface-500">System RAM</p>
                <p className="text-lg font-semibold text-surface-900 dark:text-white">
                  {formatValue(recommendations?.total_ram_gb ? `${recommendations.total_ram_gb.toFixed(1)} GB` : undefined)}
                </p>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800">
                <p className="text-xs text-surface-500">Max Concurrent Models</p>
                <p className="text-lg font-semibold text-surface-900 dark:text-white">
                  {formatValue(recommendations?.max_concurrent_models)}
                </p>
              </div>
            </div>
          </Card>
        </div>

        <div className="space-y-6">
          <Card variant="gradient" padding="lg" className="border border-brand-200/60 dark:border-brand-800/60">
            <div className="flex items-start gap-3">
              <div className="w-10 h-10 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
                <Info className="w-5 h-5 text-brand-600 dark:text-brand-400" />
              </div>
              <div>
                <p className="text-sm font-semibold text-surface-900 dark:text-white">Configuration details</p>
                <p className="text-sm text-surface-600 dark:text-surface-400">
                  Updates are written to the config file and applied immediately when possible. Fields
                  marked as restart-required will take effect after restarting the server.
                </p>
              </div>
            </div>
          </Card>

          <Card className="space-y-4">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="text-lg font-semibold text-surface-900 dark:text-white">Feature Flags</h2>
                <p className="text-sm text-surface-500">Capabilities detected in the running build.</p>
              </div>
              <Badge variant="default">Runtime</Badge>
            </div>
            <div className="grid grid-cols-1 gap-3">
              {featureRows.map((feature) => {
                const enabled = feature.enabled === true;
                const disabled = feature.enabled === false;
                return (
                  <div key={feature.key} className="flex items-center justify-between p-3 rounded-lg border border-surface-200 dark:border-surface-700">
                    <div className="flex items-center gap-3">
                      <div className={`w-9 h-9 rounded-lg flex items-center justify-center ${enabled ? 'bg-success-100 dark:bg-success-900/30' : disabled ? 'bg-error-100 dark:bg-error-900/30' : 'bg-surface-100 dark:bg-surface-800'}`}>
                        {enabled ? (
                          <CheckCircle2 className="w-4 h-4 text-success-600 dark:text-success-400" />
                        ) : disabled ? (
                          <XCircle className="w-4 h-4 text-error-600 dark:text-error-400" />
                        ) : (
                          <AlertTriangle className="w-4 h-4 text-surface-400" />
                        )}
                      </div>
                      <div>
                        <p className="text-sm font-medium text-surface-900 dark:text-white">{feature.label}</p>
                        <p className="text-xs text-surface-500">{feature.description}</p>
                      </div>
                    </div>
                    <Badge variant={enabled ? 'success' : disabled ? 'error' : 'default'}>
                      {enabled ? 'Enabled' : disabled ? 'Disabled' : 'Unknown'}
                    </Badge>
                  </div>
                );
              })}
            </div>
          </Card>

          <Card className="space-y-4">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="text-lg font-semibold text-surface-900 dark:text-white">Build Highlights</h2>
                <p className="text-sm text-surface-500">What this local build is optimized for.</p>
              </div>
              <Badge variant="default">Local-only</Badge>
            </div>
            <div className="grid grid-cols-1 gap-3">
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800 flex items-center gap-3">
                <div className="w-9 h-9 rounded-lg bg-brand-100 dark:bg-brand-900/30 flex items-center justify-center">
                  <Cpu className="w-4 h-4 text-brand-600 dark:text-brand-400" />
                </div>
                <div>
                  <p className="text-sm font-medium text-surface-900 dark:text-white">LLM + vPID</p>
                  <p className="text-xs text-surface-500">Multi-model orchestration</p>
                </div>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800 flex items-center gap-3">
                <div className="w-9 h-9 rounded-lg bg-warning-100 dark:bg-warning-900/30 flex items-center justify-center">
                  <Sparkles className="w-4 h-4 text-warning-600 dark:text-warning-400" />
                </div>
                <div>
                  <p className="text-sm font-medium text-surface-900 dark:text-white">Vision + Diffusion</p>
                  <p className="text-xs text-surface-500">Image understanding and generation</p>
                </div>
              </div>
              <div className="p-3 rounded-lg bg-surface-50 dark:bg-surface-800 flex items-center gap-3">
                <div className="w-9 h-9 rounded-lg bg-success-100 dark:bg-success-900/30 flex items-center justify-center">
                  <Server className="w-4 h-4 text-success-600 dark:text-success-400" />
                </div>
                <div>
                  <p className="text-sm font-medium text-surface-900 dark:text-white">Local API</p>
                  <p className="text-xs text-surface-500">No auth, localhost only</p>
                </div>
              </div>
            </div>
          </Card>
        </div>
      </div>
    </div>
  );
}
