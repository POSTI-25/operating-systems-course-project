import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir: './frontend/e2e', timeout: 60000, workers: 1,
  use: { baseURL: 'http://127.0.0.1:4317', headless: true, viewport: { width: 1440, height: 1000 }, trace: 'retain-on-failure' },
  outputDir: 'output/browser-tests',
  webServer: { command: 'npm run start', url: 'http://127.0.0.1:4317/api/health', reuseExistingServer: true, timeout: 30000 },
});
