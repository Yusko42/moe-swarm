import { defineConfig } from 'vite';
import { mockApi } from './dev/mock-api.ts';

export default defineConfig({
  base: './',
  plugins: [mockApi()],
  server: { port: 5173, strictPort: true },
  preview: { port: 4173, strictPort: true },
  build: { target: 'es2022' },
});
