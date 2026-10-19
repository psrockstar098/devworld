// Post-build: copy the hashed extension-host worker to the fixed URL that
// MonacoEnvironment.getWorkerUrl('extensionHostWorkerMain') serves to the
// extension-host iframe bootstrap. (Vite builds ?worker imports in a separate
// Rollup pass, so a writeBundle plugin on the main build never sees the chunk;
// do it explicitly here instead.)
import { readdirSync, mkdirSync, copyFileSync, existsSync } from 'fs'
import { join } from 'path'

const outDir = new URL('../dist/', import.meta.url).pathname
const assets = join(outDir, 'assets')
const match = readdirSync(assets).find((f) => /^extensionHost\.worker-[A-Za-z0-9]+\.js$/.test(f))
if (!match) {
  console.error('copy-worker: extensionHost.worker-*.js not found in dist/assets')
  process.exit(1)
}
const destDir = join(outDir, 'workers')
mkdirSync(destDir, { recursive: true })
copyFileSync(join(assets, match), join(destDir, 'extensionHost.worker.js'))
const mapSrc = join(assets, match + '.map')
if (existsSync(mapSrc)) copyFileSync(mapSrc, join(destDir, 'extensionHost.worker.js.map'))
console.log('copy-worker: dist/workers/extensionHost.worker.js ready')
