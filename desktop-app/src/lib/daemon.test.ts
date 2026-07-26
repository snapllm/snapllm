import { describe, expect, it } from 'vitest';
import { ensureDaemonRunning } from './daemon';

describe('daemon lifecycle bridge', () => {
  it('does not attempt native process control in a browser build', async () => {
    await expect(ensureDaemonRunning()).resolves.toBe(false);
  });
});
