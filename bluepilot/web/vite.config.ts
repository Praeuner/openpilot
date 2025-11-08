import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],

  // Disable public directory to avoid conflicts with build output
  publicDir: false,

  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },

  // Build configuration to output to public/ folder
  build: {
    outDir: 'public',
    emptyOutDir: false, // Don't delete vendor/ folder

    rollupOptions: {
      output: {
        // Organize built assets
        assetFileNames: (assetInfo) => {
          const info = assetInfo.name.split('.')
          const ext = info[info.length - 1]

          if (/png|jpe?g|svg|gif|tiff|bmp|ico/i.test(ext)) {
            return 'assets/images/[name]-[hash][extname]'
          } else if (/woff2?|ttf|eot/i.test(ext)) {
            return 'assets/fonts/[name]-[hash][extname]'
          }

          return 'assets/[name]-[hash][extname]'
        },
        chunkFileNames: 'assets/[name]-[hash].js',
        entryFileNames: 'assets/[name]-[hash].js',
      },
    },

    // Optimize chunk splitting for better caching
    chunkSizeWarningLimit: 1000,

    // Minification settings
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: false, // Keep console.logs for debugging
      },
    },
  },

  // Dev server configuration
  server: {
    port: 5173,
    strictPort: false,
    host: true, // Listen on all addresses

    // Proxy API requests to Python backend during development
    proxy: {
      '/api': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
      '/_internal': {
        target: 'http://localhost:8088',
        changeOrigin: true,
      },
    },
  },
})
