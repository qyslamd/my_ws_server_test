import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// 产物放到 www/v1/eph205，由 v1 底座以静态目录提供。
// 开发时用 Vite dev server，/api 与 /ws 代理到本地 eph205_server。
export default defineConfig({
  plugins: [react()],
  base: './',
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:8790',
      '/ws': { target: 'ws://127.0.0.1:8790', ws: true },
    },
  },
  build: {
    outDir: '../../www/v1/eph205',
    emptyOutDir: true,
  },
})
