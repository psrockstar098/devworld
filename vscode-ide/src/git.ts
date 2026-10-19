// DEVWORLD Git webview view — hosts the isomorphic-git bundle (built as a
// separate entry, inlined at runtime) and bridges VFS sync over postMessage.

import * as vscode from 'vscode'
import { vfs } from './vfs'

function snapshot(): Record<string, { content: string }> {
  const out: Record<string, { content: string }> = {}
  for (const p of vfs.list()) out[p] = { content: vfs.read(p) || '' }
  return out
}

export function registerGitView(api: typeof vscode): void {
  const provider: vscode.WebviewViewProvider = {
    resolveWebviewView(view) {
      view.webview.options = { enableScripts: true }
      const sendVfs = () => view.webview.postMessage({ type: 'vfs', files: snapshot() })
      view.webview.onDidReceiveMessage((msg: any) => {
        if (msg && msg.type === 'ready') return sendVfs()
        if (msg && msg.type === 'syncToVfs' && msg.files) {
          // checkout/branch changed the workdir — apply to the real VFS
          const incoming = msg.files as Record<string, { content: string }>
          const current = new Set(vfs.list())
          for (const [p, f] of Object.entries(incoming)) {
            vfs.write(p, f.content)
            current.delete(p)
          }
          // files deleted by the checkout disappear from the snapshot
          for (const p of current) vfs.remove(p)
          sendVfs()
        }
      })
      // inline the bundle: same-origin fetch, no webview resource config needed
      const jsUrl = new URL('gitview.js', document.baseURI).toString()
      view.webview.html = `<!DOCTYPE html><html><head><meta charset="utf-8"></head><body>
        <div id="boot" style="font:12px sans-serif;color:#8b949e;padding:8px">Loading git…</div>
        <script>
          fetch(${JSON.stringify(jsUrl)}).then(r => r.text()).then(js => {
            const s = document.createElement('script');
            s.type = 'module'; s.textContent = js;
            document.getElementById('boot').remove();
            document.body.appendChild(s);
          }).catch(e => { document.getElementById('boot').textContent = 'Failed to load git bundle.'; });
        </` + `script></body></html>`
      // refresh the snapshot whenever the view becomes visible
      view.onDidChangeVisibility(() => { if (view.visible) sendVfs() })
    }
  }
  api.window.registerWebviewViewProvider('devworldGit', provider)
}
