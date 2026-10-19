import { defineConfig } from 'vite'
import * as path from 'path'
import vsixPlugin from '@codingame/monaco-vscode-rollup-vsix-plugin'
import { textmateVirtualMainFix } from './src/shims/textmate-plugin'
import { workerUrlsPlugin } from './src/shims/worker-urls-plugin'

export default defineConfig({
  base: './',
  resolve: {
    alias: {
      // @xterm/addon-ligatures imports node:diagnostics_channel for font
      // diagnostics; stub it out for the browser.
      'node:diagnostics_channel': path.resolve(__dirname, 'src/shims/diagnostics-channel.ts'),
      // ESM wrapper for vscode-textmate (see textmate-plugin).
      'textmate-main-shim': path.resolve(__dirname, 'src/shims/textmate-main.ts')
    }
  },
  build: {
    target: 'esnext',
    outDir: 'dist',
    chunkSizeWarningLimit: 20000,
    rollupOptions: {
      treeshake: {
        // Default extensions register themselves via side effects on import,
        // but declare sideEffects:null — force-keep them.
        moduleSideEffects: (id: string) => id.includes('default-extension') ? true : undefined
      },
      input: {
        main: 'index.html',
        gitview: 'src/gitview-entry.ts'
      },
      output: {
        entryFileNames: '[name].js',
        chunkFileNames: 'chunks/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]'
      }
    }
  },
  worker: {
    format: 'es'
  },
  plugins: [textmateVirtualMainFix(), workerUrlsPlugin(), vsixPlugin()],
  esbuild: {
    minifySyntax: false
  }
})
