import { defineConfig, devices } from '@playwright/test';

export default defineConfig({
  testDir: './e2e',
  timeout: 120_000,
  expect: { timeout: 15_000 },
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: [['list'], ['html', { open: 'never' }]],
  use: {
    baseURL: 'http://127.0.0.1:9780',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'retain-on-failure',
  },
  projects: [
    {
      name: 'desktop-chromium',
      use: { ...devices['Desktop Chrome'] },
    },
    {
      name: 'mobile-chromium',
      use: { ...devices['Pixel 7'] },
      grep: /responsive/,
    },
  ],
  webServer: [
    {
      command: 'npm run dev -- --host 127.0.0.1',
      url: 'http://127.0.0.1:9780',
      // Never attach to a stale Vite process from a previous run. Reusing a
      // process that exits midway makes later tests fail with ECONNREFUSED.
      reuseExistingServer: false,
      timeout: 120_000,
    },
    ...(process.env.SNAPLLM_E2E_API_COMMAND ? [{
      command: process.env.SNAPLLM_E2E_API_COMMAND,
      url: 'http://127.0.0.1:6930/health',
      reuseExistingServer: true,
      timeout: 120_000,
    }] : []),
  ],
});
