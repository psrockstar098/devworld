// Vite plugin: the workbench's extension host bootstraps through an iframe and
// needs MonacoEnvironment.getWorkerUrl('extensionHostWorkerMain') to return a
// real URL. Vite builds ?worker entries as hashed chunks in a separate pass,
// so we copy the extension-host worker to a fixed, deterministic URL after the
// build finishes.
import * as fs from 'fs'
import * as path from 'path'

export function workerUrlsPlugin() {
  return {
    name: 'worker-urls-manifest',
    writeBundle(_opts: unknown, bundle: Record<string, any>) {
      const outDir = (_opts as any).dir as string
      for (const [fileName, chunk] of Object.entries(bundle)) {
        if (chunk.type !== 'chunk') continue
        const base = fileName.split('/').pop() || ''
        if (base.startsWith('extensionHost.worker-') && base.endsWith('.js')) {
          const src = path.join(outDir, fileName)
          const destDir = path.join(outDir, 'workers')
          const dest = path.join(destDir, 'extensionHost.worker.js')
          fs.mkdirSync(destDir, { recursive: true })
          fs.copyFileSync(src, dest)
          // also copy the sourcemap if present
          const mapSrc = src + '.map'
          if (fs.existsSync(mapSrc)) fs.copyFileSync(mapSrc, dest + '.map')
        }
      }
    }
  }
}

// Fixed URL (relative to the page) for the extension-host worker.
export const EXTENSION_HOST_WORKER_URL = './workers/extensionHost.worker.js'
