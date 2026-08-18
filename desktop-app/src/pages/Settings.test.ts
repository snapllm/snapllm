import { describe, expect, it } from 'vitest';
import { buildConfigUpdatePayload, SettingsFormState } from './Settings';

const form: SettingsFormState = {
  host: '127.0.0.1',
  port: '6930',
  workspace_root: 'D:/SnapLLM/workspace',
  default_models_path: 'D:/Models',
  cors_enabled: true,
  timeout_seconds: '600',
  max_concurrent_requests: '8',
  max_active_inferences: '2',
  max_models: '10',
  default_ram_budget_mb: '16384',
  default_strategy: 'balanced',
  enable_gpu: true,
};

describe('settings persistence contract', () => {
  it('keeps the user-selected model path in the server update payload', () => {
    const payload = buildConfigUpdatePayload(form);
    expect(payload.workspace?.default_models_path).toBe('D:/Models');
    expect(payload.server?.max_active_inferences).toBe(2);
  });
});
