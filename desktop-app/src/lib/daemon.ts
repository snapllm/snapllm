import { invoke } from '@tauri-apps/api/core';
import { isTauriAvailable } from './api';

export type DaemonAction = 'start' | 'stop' | 'status';

export const daemonCommand = async (action: DaemonAction): Promise<string> => {
  if (!isTauriAvailable()) throw new Error('Daemon controls are available in SnapLLM Desktop.');
  const command = action === 'start' ? 'daemon_start' : action === 'stop' ? 'daemon_stop' : 'daemon_status';
  return invoke<string>(command);
};

export const ensureDaemonRunning = async (): Promise<boolean> => {
  if (!isTauriAvailable()) return false;
  try {
    const status = await daemonCommand('status');
    if (/running/i.test(status)) return true;
    await daemonCommand('start');
    // Starting the child is asynchronous. Wait for the HTTP listener before
    // allowing the UI queries to conclude that the daemon is offline.
    for (let attempt = 0; attempt < 20; attempt += 1) {
      try {
        const response = await fetch('http://127.0.0.1:6930/health', { cache: 'no-store' });
        if (response.ok) return true;
      } catch {
        // The child may still be binding its socket.
      }
      await new Promise((resolve) => window.setTimeout(resolve, 250));
    }
    return false;
  } catch { return false; }
};
