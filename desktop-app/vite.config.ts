import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import packageJson from './package.json'

const apiTarget = 'http://127.0.0.1:6930'
const apiProxy = () => ({
  target: apiTarget,
  changeOrigin: true,
})

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  define: {
    __APP_VERSION__: JSON.stringify(packageJson.version),
  },

  // Tauri expects a fixed port will fail if that port is already used
  server: {
    port: 9780,
    strictPort: true,
    proxy: {
      '/api': apiProxy(),
      '/v1': apiProxy(),
      '/health': apiProxy(),
    },
  },

  // to make use of `TAURI_DEBUG` and other env variables
  // https://tauri.studio/v1/api/config#buildconfig.beforedevcommand
  envPrefix: ['VITE_', 'TAURI_'],

  build: {
    // Match the TypeScript output and the evergreen WebViews supported by Tauri.
    target: 'es2020',
    // don't minify for debug builds
    minify: !process.env.TAURI_DEBUG ? 'esbuild' : false,
    // produce sourcemaps for debug builds
    sourcemap: !!process.env.TAURI_DEBUG,
  },
})
