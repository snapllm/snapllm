import { invoke } from '@tauri-apps/api/core';
import { isTauriAvailable } from './api';

export type DaemonAction = 'start' | 'stop' | 'status';

export const daemonCommand = async (action: DaemonAction): Promise<string> => {
  if (!isTauriAvailable()) throw new Error('Daemon controls are available in SnapLLM Desktop.');
  const command = action === 'start' ? 'daemon_start' : action === 'stop' ? 'daemon_stop' : 'daemon_status';
  return invoke<string>(command);
};

const daemonHealthUrl = 'http://127.0.0.1:6930/health';

const daemonHealthy = async (): Promise<boolean> => {
  try {
    const response = await fetch(daemonHealthUrl, { cache: 'no-store' });
    return response.ok;
  } catch {
    return false;
  }
};

export const ensureDaemonRunning = async (): Promise<boolean> => {
  if (!isTauriAvailable()) return false;
  try {
    const status = await daemonCommand('status');
    if (/running/i.test(status) && await daemonHealthy()) {
      // Attach the Tauri supervisor to a daemon started by the CLI/login task.
      // daemon_start is idempotent when the port is already healthy.
      await daemonCommand('start');
      return true;
    }
    // A stale PID or wedged process must not block recovery. The native stop
    // command removes stale state and is safe to ignore when already stopped.
    if (/running/i.test(status)) {
      try { await daemonCommand('stop'); } catch { /* continue with restart */ }
    }
    await daemonCommand('start');
    // Starting the child is asynchronous. Wait for the HTTP listener before
    // allowing the UI queries to conclude that the daemon is offline.
    for (let attempt = 0; attempt < 20; attempt += 1) {
      try {
        if (await daemonHealthy()) return true;
      } catch {
        // The child may still be binding its socket.
      }
      await new Promise((resolve) => window.setTimeout(resolve, 250));
    }
    return false;
  } catch { return false; }
};
