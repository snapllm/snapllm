import { describe, expect, it, vi } from 'vitest';
import { ensureDaemonRunning } from './daemon';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(async (command: string) => command === 'daemon_status' ? 'running (pid 42)' : 'attached'),
}));

describe('daemon lifecycle bridge', () => {
  it('does not attempt native process control in a browser build', async () => {
    await expect(ensureDaemonRunning()).resolves.toBe(false);
  });

  it('attaches the supervisor only after a healthy daemon probe', async () => {
    vi.stubGlobal('window', { __TAURI__: {}, setTimeout: (callback: () => void) => { callback(); return 0; } });
    const fetchMock = vi.fn().mockResolvedValue({ ok: true });
    vi.stubGlobal('fetch', fetchMock);

    await expect(ensureDaemonRunning()).resolves.toBe(true);
    const { invoke } = await import('@tauri-apps/api/core');
    expect(invoke).toHaveBeenCalledWith('daemon_status');
    expect(invoke).toHaveBeenCalledWith('daemon_start');
    expect(fetchMock).toHaveBeenCalledWith('http://127.0.0.1:6930/health', { cache: 'no-store' });

    vi.unstubAllGlobals();
  });
});
