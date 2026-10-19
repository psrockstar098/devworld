// DEVWORLD git webview bundle (built as a separate entry, inlined into the
// webview). isomorphic-git, fully local. Syncs the workdir with the main
// world over postMessage: main sends {type:'vfs', files}, we reply with
// {type:'syncToVfs', files} after checkout/branch.

import * as git from 'isomorphic-git'
import { Buffer } from 'buffer'

;(window as any).Buffer = Buffer

const vscodeApi: any = (window as any).acquireVsCodeApi
  ? (window as any).acquireVsCodeApi()
  : { postMessage: () => {} }

const te = new TextEncoder()
const td = new TextDecoder()

function enoent(p: string): any {
  const e: any = new Error(`ENOENT: no such file or directory, '${p}'`)
  e.code = 'ENOENT'
  return e
}
function enotempty(p: string): any {
  const e: any = new Error('ENOTEMPTY: directory not empty')
  e.code = 'ENOTEMPTY'
  return e
}

const GITFS_KEY = 'devworld.gitfs.v1'

const GitFS: any = {
  store: {} as Record<string, Uint8Array>,
  norm(p: string): string {
    return String(p).split('/').filter((s) => s !== '' && s !== '.').join('/')
  },
  isDir(p: string): boolean {
    p = this.norm(p)
    if (p === '') return true
    return Object.keys(this.store).some((k) => k === p || k.startsWith(p + '/'))
  },
  isFile(p: string): boolean { return this.norm(p) in this.store },
  _stat(p: string): any {
    p = this.norm(p)
    const dir = this.isDir(p) && !this.isFile(p)
    if (!dir && !this.isFile(p)) throw enoent(p)
    return {
      isDirectory: () => dir,
      isFile: () => !dir,
      isSymbolicLink: () => false,
      mode: dir ? 0o040000 : 0o100644,
      size: dir ? 0 : this.store[p].length,
      mtimeMs: Date.now(), mtime: new Date(), ctimeMs: Date.now(), ctime: new Date(),
      dev: 0, ino: 0
    }
  },
  async readFile(p: string, options?: any): Promise<any> {
    p = this.norm(p)
    if (!(p in this.store)) throw enoent(p)
    const data = this.store[p].slice()
    const enc = typeof options === 'string' ? options : options && options.encoding
    if (enc === 'utf8' || enc === 'utf-8') return td.decode(data)
    return data
  },
  async writeFile(p: string, data: any): Promise<void> {
    p = this.norm(p)
    if (typeof data === 'string') data = te.encode(data)
    this.store[p] = data instanceof Uint8Array ? data.slice() : Uint8Array.from(data)
    this.persistSoon()
  },
  async unlink(p: string): Promise<void> {
    p = this.norm(p)
    if (!(p in this.store)) throw enoent(p)
    delete this.store[p]
    this.persistSoon()
  },
  async mkdir(_p: string): Promise<void> { /* dirs are implicit */ },
  async rmdir(p: string): Promise<void> {
    p = this.norm(p)
    if (!this.isDir(p)) throw enoent(p)
    const kids = Object.keys(this.store).filter((k) => k !== p && k.startsWith(p === '' ? '' : p + '/'))
    if (kids.length) throw enotempty(p)
  },
  async readdir(p: string): Promise<string[]> {
    p = this.norm(p)
    if (!this.isDir(p) || (p !== '' && this.isFile(p))) throw enoent(p)
    const out = new Set<string>()
    Object.keys(this.store).forEach((k) => {
      if (p === '') { out.add(k.split('/')[0]); return }
      if (k === p || !k.startsWith(p + '/')) return
      out.add(k.slice(p.length + 1).split('/')[0])
    })
    return [...out].sort()
  },
  async stat(p: string): Promise<any> { return this._stat(p) },
  async lstat(p: string): Promise<any> { return this._stat(p) },
  async readlink(): Promise<never> { throw new Error('readlink not supported') },
  async symlink(): Promise<never> { throw new Error('symlink not supported') },
  _t: null as any,
  persistSoon(): void {
    clearTimeout(this._t)
    this._t = setTimeout(() => this.persist(), 500)
  },
  persist(): void {
    try {
      const o: Record<string, string> = {}
      for (const k of Object.keys(this.store)) {
        const b = this.store[k]
        let s = ''
        for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i])
        o[k] = btoa(s)
      }
      localStorage.setItem(GITFS_KEY, JSON.stringify(o))
    } catch { /* ignore */ }
  },
  load(): void {
    try {
      const raw = localStorage.getItem(GITFS_KEY)
      if (!raw) return
      const o = JSON.parse(raw)
      for (const k of Object.keys(o)) {
        const s = atob(o[k])
        const b = new Uint8Array(s.length)
        for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i)
        this.store[k] = b
      }
    } catch { /* ignore */ }
  },
  syncFromVFS(files: Record<string, { content: string }>): void {
    // keep .git/*, replace workdir files with the VFS snapshot
    const keep: Record<string, Uint8Array> = {}
    for (const k of Object.keys(this.store)) {
      if (k === '.git' || k.startsWith('.git/')) keep[k] = this.store[k]
    }
    this.store = keep
    for (const [p, f] of Object.entries(files)) {
      this.store[p] = te.encode(f.content)
    }
  },
  workdirFiles(): Record<string, { content: string }> {
    const out: Record<string, { content: string }> = {}
    for (const k of Object.keys(this.store)) {
      if (k === '.git' || k.startsWith('.git/')) continue
      out[k] = { content: td.decode(this.store[k]) }
    }
    return out
  }
}

function escHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function lineDiff(a: string, b: string): Array<{ t: string; s: string }> {
  const A = a.split('\n'), B = b.split('\n')
  if (A.length * B.length > 4000000) return [{ t: ' ', s: '(file too large for inline diff)' }]
  const N = A.length, M = B.length
  const dp: Uint16Array[] = Array.from({ length: N + 1 }, () => new Uint16Array(M + 1))
  for (let i = N - 1; i >= 0; i--) {
    for (let j = M - 1; j >= 0; j--) {
      dp[i][j] = A[i] === B[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1])
    }
  }
  const out: Array<{ t: string; s: string }> = []
  let i = 0, j = 0
  while (i < N && j < M) {
    if (A[i] === B[j]) { out.push({ t: ' ', s: A[i] }); i++; j++ }
    else if (dp[i + 1][j] >= dp[i][j + 1]) { out.push({ t: '-', s: A[i] }); i++ }
    else { out.push({ t: '+', s: B[j] }); j++ }
  }
  while (i < N) out.push({ t: '-', s: A[i++] })
  while (j < M) out.push({ t: '+', s: B[j++] })
  return out
}

const GitUI = {
  inited: false,

  statusLabel(row: any[]): [string, string] {
    const [, head, workdir, stage] = row
    if (workdir === 0 && head === 1) return ['deleted', 'm']
    if (head === 0 && workdir === 2) return [stage === 2 ? 'added' : 'new', 'a']
    if (workdir === 2 && head === 1 && stage !== 2) return ['modified', 'm']
    if (stage === 2 || stage === 3) return ['staged', 'a']
    return ['changed', 'm']
  },

  async ensure(): Promise<void> {
    try {
      await git.currentBranch({ fs: GitFS, dir: '/' })
      this.inited = true
    } catch {
      this.inited = false
    }
    await this.render()
  },

  async render(): Promise<void> {
    const host = document.getElementById('gitPanel')!
    host.innerHTML = `
      <p class="subtle" id="gitState">…</p>
      <div>
        <button class="small" id="gInit">Init repo</button>
        <button class="small ghost" id="gRefresh">Refresh</button>
        <button class="small ghost" id="gStageAll">Stage all</button>
      </div>
      <div class="pkg-row"><input class="sinput" id="gMsg" placeholder="commit message" style="margin:4px 0"><button class="small" id="gCommit">Commit</button></div>
      <div class="shead">CHANGES</div><div id="gChanges"></div>
      <div class="shead">DIFF <span class="subtle" id="gDiffFile"></span></div><pre class="diffview" id="gDiff">(select a changed file)</pre>
      <div class="shead">BRANCHES</div><div id="gBranches" style="font-size:12px"></div>
      <div class="pkg-row"><input class="sinput" id="gBranchName" placeholder="new branch"><button class="small ghost" id="gBranch">Create + switch</button></div>
      <div class="shead">LOG</div><pre class="log" id="gLog"></pre>
      <p class="subtle" style="margin-top:6px">Local git only — no remote, no push yet.</p>`
    ;(host.querySelector('#gInit') as HTMLElement).addEventListener('click', () => void this.init())
    ;(host.querySelector('#gRefresh') as HTMLElement).addEventListener('click', () => void this.refreshState())
    ;(host.querySelector('#gStageAll') as HTMLElement).addEventListener('click', () => void this.stageAll())
    ;(host.querySelector('#gCommit') as HTMLElement).addEventListener('click', () =>
      void this.commit((host.querySelector('#gMsg') as HTMLInputElement).value))
    ;(host.querySelector('#gBranch') as HTMLElement).addEventListener('click', () =>
      void this.createBranch((host.querySelector('#gBranchName') as HTMLInputElement).value.trim()))

    if (!this.inited) {
      ;(host.querySelector('#gitState') as HTMLElement).textContent = 'Not a git repo yet — hit Init repo.'
      return
    }
    await this.refreshState()
  },

  async init(): Promise<void> {
    await git.init({ fs: GitFS, dir: '/', defaultBranch: 'main' })
    this.inited = true
    await this.render()
  },

  async refreshState(): Promise<void> {
    const host = document.getElementById('gitPanel')!
    const branch = await git.currentBranch({ fs: GitFS, dir: '/' }).catch(() => '(detached)')
    const matrix = await git.statusMatrix({ fs: GitFS, dir: '/' })
    const changed = matrix.filter((r) => !(r[1] === 1 && r[2] === 1 && r[3] === 1))
    ;(host.querySelector('#gitState') as HTMLElement).textContent =
      `on branch ${branch} — ${changed.length ? changed.length + ' change(s)' : 'clean'}`

    const ch = host.querySelector('#gChanges')!
    ch.innerHTML = ''
    if (!changed.length) ch.innerHTML = '<p class="subtle">No changes.</p>'
    changed.forEach((r) => {
      const [fp] = r
      const [label, cls] = this.statusLabel(r)
      const div = document.createElement('div')
      div.className = 'st-row'
      div.innerHTML = `<span class="${cls}">[${label}]</span> <span></span> <button class="ghost small" style="float:right">stage</button>`
      ;(div.querySelector('span:nth-child(2)') as HTMLElement).textContent = fp
      ;(div.querySelector('button') as HTMLElement).addEventListener('click', async () => {
        await git.add({ fs: GitFS, dir: '/', filepath: fp })
        await this.render()
      })
      div.addEventListener('click', (e) => {
        if ((e.target as HTMLElement).tagName !== 'BUTTON') void this.showDiff(fp)
      })
      ch.appendChild(div)
    })

    const log = await git.log({ fs: GitFS, dir: '/', depth: 20 }).catch(() => [])
    ;(host.querySelector('#gLog') as HTMLElement).textContent = log.length
      ? log.map((c: any) => {
          const cm = c.commit || {}
          const ts = cm.committer && cm.committer.timestamp
            ? new Date(cm.committer.timestamp * 1000).toLocaleString() : ''
          const msg = (cm.message || '').trim().split('\n')[0]
          return `${c.oid.slice(0, 7)}  ${ts}\n    ${msg}`
        }).join('\n\n')
      : '(no commits yet)'

    const branches = await git.listBranches({ fs: GitFS, dir: '/' }).catch(() => [])
    const bh = host.querySelector('#gBranches')!
    bh.innerHTML = ''
    branches.forEach((b: string) => {
      const s = document.createElement('span')
      s.style.cssText = 'margin-right:8px;cursor:pointer' + (b === branch ? ';color:#3fb950;font-weight:700' : ';color:#79c0ff')
      s.textContent = (b === branch ? '● ' : '') + b
      s.title = 'Switch to ' + b
      s.addEventListener('click', () => void this.checkout(b))
      bh.appendChild(s)
    })
  },

  async stageAll(): Promise<void> {
    const matrix = await git.statusMatrix({ fs: GitFS, dir: '/' })
    for (const [fp, head, workdir] of matrix) {
      if (workdir === 2) await git.add({ fs: GitFS, dir: '/', filepath: fp })
      else if (workdir === 0 && head === 1) await git.remove({ fs: GitFS, dir: '/', filepath: fp })
    }
    await this.render()
  },

  async commit(message: string): Promise<void> {
    const note = document.getElementById('gitNote')!
    if (!message || !message.trim()) { note.textContent = 'Write a commit message first.'; return }
    try {
      const oid = await git.commit({
        fs: GitFS, dir: '/',
        message: message.trim(),
        author: { name: 'devworld-player', email: 'player@devworld.local' }
      })
      note.textContent = `Committed ${oid.slice(0, 7)}.`
    } catch (e: any) {
      note.textContent = 'Commit failed: ' + ((e && e.message) || e)
    }
    await this.render()
  },

  async showDiff(fp: string): Promise<void> {
    const diffEl = document.getElementById('gDiff')!
    ;(document.getElementById('gDiffFile') as HTMLElement).textContent = '· ' + fp
    let headContent = ''
    try {
      const { blob } = await git.readBlob({ fs: GitFS, dir: '/', oid: 'HEAD', filepath: fp })
      headContent = td.decode(blob)
    } catch { headContent = '' }
    const workContent = GitFS.store[fp] ? td.decode(GitFS.store[fp]) : ''
    const parts = lineDiff(headContent, workContent)
    diffEl.innerHTML = parts.slice(0, 400).map((p) =>
      `<span class="${p.t === '-' ? 'del' : p.t === '+' ? 'add' : ''}">${escHtml(p.t + ' ' + p.s)}</span>`
    ).join('\n')
  },

  async createBranch(name: string): Promise<void> {
    if (!name) return
    try {
      await git.branch({ fs: GitFS, dir: '/', ref: name, checkout: true })
      vscodeApi.postMessage({ type: 'syncToVfs', files: GitFS.workdirFiles() })
      ;(document.getElementById('gitNote') as HTMLElement).textContent = `Switched to new branch ${name}.`
    } catch (e: any) {
      ;(document.getElementById('gitNote') as HTMLElement).textContent = 'Branch failed: ' + ((e && e.message) || e)
    }
    await this.render()
  },

  async checkout(name: string): Promise<void> {
    try {
      await git.checkout({ fs: GitFS, dir: '/', ref: name })
      vscodeApi.postMessage({ type: 'syncToVfs', files: GitFS.workdirFiles() })
      ;(document.getElementById('gitNote') as HTMLElement).textContent = `Switched to branch ${name}.`
    } catch (e: any) {
      ;(document.getElementById('gitNote') as HTMLElement).textContent = 'Checkout failed: ' + ((e && e.message) || e)
    }
    await this.render()
  }
}

window.addEventListener('message', (event) => {
  const msg = event.data
  if (msg && msg.type === 'vfs' && msg.files) {
    GitFS.syncFromVFS(msg.files)
    void GitUI.ensure()
  }
})

GitFS.load()

document.body.innerHTML = `
<style>
  body { font: 12px/1.5 -apple-system, 'Segoe UI', sans-serif; color: #cccccc; background: transparent; padding: 8px; margin: 0; }
  .subtle { color: #8b949e; } .shead { font-weight: 700; margin: 8px 0 4px; font-size: 11px; text-transform: uppercase; letter-spacing: .04em; }
  button { background: #0e639c; color: #fff; border: 0; border-radius: 4px; padding: 4px 10px; cursor: pointer; font-size: 12px; }
  button.ghost { background: transparent; border: 1px solid #3c3c3c; }
  button.small { padding: 2px 8px; font-size: 11px; }
  .sinput { background: #3c3c3c; border: 1px solid #3c3c3c; color: #cccccc; border-radius: 4px; padding: 4px 8px; font-size: 12px; width: 100%; box-sizing: border-box; }
  .pkg-row { display: flex; gap: 6px; align-items: center; }
  .st-row { padding: 3px 4px; cursor: pointer; border-radius: 3px; } .st-row:hover { background: #2a2d2e; }
  .st-row .a { color: #3fb950; } .st-row .m { color: #d29922; }
  .diffview, .log { background: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 4px; padding: 6px; white-space: pre-wrap; word-break: break-word; max-height: 220px; overflow: auto; }
  .diffview .add { color: #3fb950; } .diffview .del { color: #f85149; }
  #gitNote { min-height: 18px; color: #8b949e; }
</style>
<div id="gitPanel"></div><p id="gitNote"></p>`

vscodeApi.postMessage({ type: 'ready' })
void GitUI.render()
