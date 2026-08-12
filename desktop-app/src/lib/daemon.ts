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
    return true;
  } catch { return false; }
};
