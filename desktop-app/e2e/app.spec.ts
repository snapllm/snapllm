import { expect, test } from '@playwright/test';
import type { Page, Route } from '@playwright/test';

const routes = [
  ['/', 'Dashboard'],
  ['/chat', 'Chat'],
  ['/images', 'Image Studio'],
  ['/vision', 'Vision'],
  ['/models', 'Models'],
  ['/compare', 'A/B Compare'],
  ['/switch', 'Quick Switch'],
  ['/contexts', 'vPID L2 Contexts'],
  ['/playground', 'API Playground'],
  ['/batch', 'Batch Processing'],
  ['/metrics', 'Metrics'],
  ['/settings', 'Server Settings'],
  ['/about', 'About'],
  ['/help', 'Help'],
] as const;

test('all public UI routes render without runtime errors', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text());
  });

  for (const [path, visibleText] of routes) {
    await page.goto(path);
    await expect(page.getByText(visibleText, { exact: false }).first()).toBeVisible();
    await expect(page.locator('body')).not.toContainText('Cannot connect to server');
  }

  expect(errors, errors.join('\n')).toEqual([]);
});

test('development proxy reaches the API and Playground executes health', async ({ page }) => {
  const response = await page.request.get('/health');
  expect(response.ok()).toBeTruthy();
  const health = await response.json();
  expect(health).toMatchObject({ status: 'ok' });
  expect(health.version).toMatch(/^\d+\.\d+\.\d+$/);

  await page.goto('/playground');
  await page.getByRole('button', { name: /send request/i }).click();
  await expect(page.getByText(/200/).first()).toBeVisible();
  await expect(page.locator('body')).toContainText('"status": "ok"');

  const browserPostStatus = await page.evaluate(async () => {
    const result = await fetch('/api/v1/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt: 'bounded browser request', max_tokens: -1 }),
    });
    return result.status;
  });
  expect(browserPostStatus).toBe(400);

  const evilOrigin = await page.request.post('/api/v1/generate', {
    headers: { Origin: 'https://evil.example' },
    data: { prompt: 'must be rejected', max_tokens: 1 },
  });
  expect(evilOrigin.status()).toBe(403);
});

test('low-weight model produces a response through the Chat UI', async ({ page }) => {
  const modelPath = process.env.SNAPLLM_E2E_MODEL_PATH;
  test.skip(!modelPath, 'Set SNAPLLM_E2E_MODEL_PATH to run live model loading');
  const load = await page.request.post('/api/v1/models/load', {
    data: {
      name: 'low-weight-ui-e2e',
      file_path: modelPath,
      strategy: 'cpu',
      n_gpu_layers: 0,
      context_size: 512,
    },
    timeout: 120_000,
  });
  expect(load.ok(), await load.text()).toBeTruthy();

  try {
    await page.goto('/chat');
    const modelSelect = page.locator('select').first();
    await expect(modelSelect.locator('option', { hasText: 'low-weight-ui-e2e' })).toHaveCount(1);
    await modelSelect.selectOption('low-weight-ui-e2e');
    await page.getByRole('button', { name: 'Settings' }).click();
    await page.locator('input[type="range"]').first().fill('64');
    const input = page.getByPlaceholder(/type your message/i);
    await expect(input).toBeEnabled();
    await input.fill('Reply briefly with hello.');
    await input.press('Enter');
    await expect(page.getByTestId('chat-message-user').last()).toContainText('Reply briefly with hello.');
    const assistant = page.getByTestId('chat-message-assistant').last();
    await expect(assistant).toBeVisible({ timeout: 120_000 });
    const responseText = (await assistant.innerText()).trim();
    expect(responseText.length).toBeGreaterThan(0);
    expect(responseText).not.toMatch(/streaming error|request failed|cannot connect/i);
  } finally {
    const unload = await page.request.post('/api/v1/models/unload', {
      data: { model_id: 'low-weight-ui-e2e' },
      timeout: 120_000,
    });
    expect(unload.ok(), await unload.text()).toBeTruthy();
  }
});

test('model management renders empty, loading, error, partial, and ideal states', async ({ browser }) => {
  const runState = async (
    handler: (route: Route) => Promise<unknown> | unknown,
    assertion: (page: Page) => Promise<void>,
  ) => {
    const page = await browser.newPage();
    await page.route('**/api/v1/models/**', handler);
    await page.goto('/models');
    await assertion(page);
    await page.close();
  };

  await runState(
    (route) => route.fulfill({ json: { status: 'success', models: [], count: 0, current_model: null } }),
    async (page) => expect(page.getByText('No models loaded')).toBeVisible(),
  );
  await runState(
    async (route) => {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      await route.fulfill({ json: { status: 'success', models: [], count: 0, current_model: null } });
    },
    async (page) => expect(page.getByText('Loading models...')).toBeVisible(),
  );
  await runState(
    (route) => route.fulfill({ status: 503, json: { error: { message: 'unavailable' } } }),
    async (page) => expect(page.getByText('Unable to load models')).toBeVisible({ timeout: 15_000 }),
  );
  await runState(
    (route) => route.fulfill({
      json: {
        status: 'success',
        models: [{ id: 'partial-model', name: 'partial-model', active: false, status: 'loaded' }],
        count: 1,
        current_model: null,
      },
    }),
    async (page) => expect(page.getByText('partial-model', { exact: true }).first()).toBeVisible(),
  );
  await runState(
    (route) => route.fulfill({
      json: {
        status: 'success',
        models: [{
          id: 'ideal-model',
          name: 'ideal-model',
          active: true,
          status: 'loaded',
          type: 'llm',
          size_bytes: 1024,
          memory_usage_mb: 1,
        }],
        count: 1,
        current_model: 'ideal-model',
      },
    }),
    async (page) => expect(page.getByText('ideal-model', { exact: true }).first()).toBeVisible(),
  );
});

test('stalled health check leaves loading and reaches the offline state', async ({ page }) => {
  await page.route('**/health', async (route) => {
    await new Promise((resolve) => setTimeout(resolve, 750));
    await route.abort('timedout');
  });
  await page.goto('/');
  await expect(page.getByText('Connecting...', { exact: true })).toBeVisible();
  await expect(page.getByText('Cannot connect to server', { exact: true })).toBeVisible({
    timeout: 10_000,
  });
});

test('responsive: primary routes avoid horizontal document overflow', async ({ page }) => {
  for (const path of ['/', '/models', '/chat', '/playground', '/settings']) {
    await page.goto(path);
    const overflow = await page.evaluate(
      () => document.documentElement.scrollWidth - document.documentElement.clientWidth,
    );
    expect(overflow, `${path} overflows horizontally`).toBeLessThanOrEqual(1);
  }
});
