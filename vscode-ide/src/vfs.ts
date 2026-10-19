import * as vscode from 'vscode'

export const VFS_KEY = 'devworld.vfs.v1'
export const SCHEME = 'file'
export const WORKSPACE_ROOT = 'workspace'

const WELCOME_README = `# Welcome to DEVWORLD

This is your game IDE — real VS Code running in the browser.

- **Files** live in the Explorer on the left (your work is saved in this browser).
- **Run** the open file with the play button or Ctrl+Enter.
- **Terminal** (Ctrl+\`) has local commands: help, ls, cat, run, exec.
- **Tests** view: define test cases per file, run them, read the report.
- **Source Control** view: git via isomorphic-git, fully local.
- **Settings** (gear icon): point the IDE at a DEVWORLD sandbox backend
  to compile and run C, C++, Java, Go, Rust, Zig and more.

Open the Command Palette (Ctrl+Shift+P) and type DEVWORLD to see game commands.
`

export interface VfsFile { content: string }

class VfsStore {
  files: Record<string, VfsFile> = {}
  private timer: ReturnType<typeof setTimeout> | null = null
  private emitter = new vscode.EventEmitter<vscode.FileChangeEvent[]>()

  readonly onDidChangeFile = this.emitter.event

  norm(p: string): string {
    return String(p).replace(/^\/+/, '').replace(/\/+/g, '/').replace(/^workspace\/?/, '')
  }

  read(path: string): string | null {
    const f = this.files[this.norm(path)]
    return f ? f.content : null
  }
  exists(path: string): boolean {
    return this.norm(path) in this.files
  }
  write(path: string, content: string): void {
    path = this.norm(path)
    this.files[path] = { content: String(content) }
    this.fire([{ type: vscode.FileChangeType.Changed, uri: this.uri(path) }])
    this.persistSoon()
  }
  create(path: string, content = ''): void {
    path = this.norm(path)
    if (path in this.files) return
    this.files[path] = { content: String(content) }
    this.fire([{ type: vscode.FileChangeType.Created, uri: this.uri(path) }])
    this.persistSoon()
  }
  remove(path: string): number {
    path = this.norm(path)
    const gone: vscode.FileChangeEvent[] = []
    for (const k of Object.keys(this.files)) {
      if (k === path || k.startsWith(path + '/')) {
        delete this.files[k]
        gone.push({ type: vscode.FileChangeType.Deleted, uri: this.uri(k) })
      }
    }
    if (gone.length) {
      this.fire(gone)
      this.persistSoon()
    }
    return gone.length
  }
  rename(oldPath: string, newPath: string): boolean {
    oldPath = this.norm(oldPath)
    newPath = this.norm(newPath)
    if (oldPath === newPath || !(oldPath in this.files)) return false
    this.files[newPath] = this.files[oldPath]
    delete this.files[oldPath]
    this.fire([
      { type: vscode.FileChangeType.Deleted, uri: this.uri(oldPath) },
      { type: vscode.FileChangeType.Created, uri: this.uri(newPath) }
    ])
    this.persistSoon()
    return true
  }
  list(): string[] {
    return Object.keys(this.files).sort()
  }
  uri(path: string): vscode.Uri {
    return vscode.Uri.from({ scheme: SCHEME, path: '/' + WORKSPACE_ROOT + '/' + this.norm(path) })
  }
  pathFromUri(uri: vscode.Uri): string {
    return this.norm(uri.path.replace(new RegExp(`^/${WORKSPACE_ROOT}/?`), ''))
  }

  private fire(e: vscode.FileChangeEvent[]): void {
    this.emitter.fire(e)
  }
  private persistSoon(): void {
    if (this.timer) clearTimeout(this.timer)
    this.timer = setTimeout(() => this.persist(), 400)
  }
  persist(): void {
    try {
      localStorage.setItem(VFS_KEY, JSON.stringify(this.files))
    } catch { /* quota — keep in memory */ }
  }
  load(): boolean {
    try {
      const raw = localStorage.getItem(VFS_KEY)
      if (raw) {
        this.files = JSON.parse(raw) || {}
        return true
      }
    } catch { /* ignore */ }
    return false
  }
  seed(): void {
    if (!this.load() || Object.keys(this.files).length === 0) {
      this.files = { 'README.md': { content: WELCOME_README } }
      this.persist()
    }
  }
}

export const vfs = new VfsStore()

export class DevworldFileSystemProvider implements vscode.FileSystemProvider {
  private emitter = new vscode.EventEmitter<vscode.FileChangeEvent[]>()
  readonly onDidChangeFile = this.emitter.event

  constructor() {
    vfs.onDidChangeFile((e) => this.emitter.fire(e as vscode.FileChangeEvent[]))
  }

  watch(): vscode.Disposable {
    return new vscode.Disposable(() => {})
  }

  stat(uri: vscode.Uri): vscode.FileStat {
    const p = vfs.pathFromUri(uri)
    if (vfs.exists(p)) {
      const size = new TextEncoder().encode(vfs.read(p) || '').length
      return { type: vscode.FileType.File, ctime: 0, mtime: Date.now(), size }
    }
    // implicit directory?
    const prefix = p ? p + '/' : ''
    for (const k of vfs.list()) {
      if (k.startsWith(prefix)) {
        return { type: vscode.FileType.Directory, ctime: 0, mtime: Date.now(), size: 0 }
      }
    }
    throw vscode.FileSystemError.FileNotFound(uri)
  }

  readDirectory(uri: vscode.Uri): [string, vscode.FileType][] {
    const dir = vfs.pathFromUri(uri)
    const prefix = dir ? dir + '/' : ''
    const files = new Set<string>()
    const dirs = new Set<string>()
    for (const k of vfs.list()) {
      if (!k.startsWith(prefix)) continue
      const rest = k.slice(prefix.length)
      if (!rest) continue
      const seg = rest.split('/')
      if (seg.length === 1) files.add(seg[0])
      else dirs.add(seg[0])
    }
    // also surface the workspace root itself
    if (!dir && vfs.list().length === 0) return []
    return [
      ...[...dirs].sort().map((d): [string, vscode.FileType] => [d, vscode.FileType.Directory]),
      ...[...files].sort().map((f): [string, vscode.FileType] => [f, vscode.FileType.File])
    ]
  }

  createDirectory(uri: vscode.Uri): void {
    // directories are implicit; create a placeholder so empty dirs render
    const p = vfs.pathFromUri(uri)
    if (p) vfs.create(p + '/.keep', '')
  }

  readFile(uri: vscode.Uri): Uint8Array {
    const c = vfs.read(vfs.pathFromUri(uri))
    if (c == null) throw vscode.FileSystemError.FileNotFound(uri)
    return new TextEncoder().encode(c)
  }

  writeFile(uri: vscode.Uri, content: Uint8Array): void {
    vfs.write(vfs.pathFromUri(uri), new TextDecoder().decode(content))
  }

  delete(uri: vscode.Uri, options: { recursive?: boolean }): void {
    const p = vfs.pathFromUri(uri)
    if (!options.recursive && vfs.list().some((k) => k.startsWith(p + '/'))) {
      throw vscode.FileSystemError.NoPermissions('directory not empty')
    }
    if (!vfs.remove(p)) throw vscode.FileSystemError.FileNotFound(uri)
  }

  rename(oldUri: vscode.Uri, newUri: vscode.Uri): void {
    const o = vfs.pathFromUri(oldUri)
    const n = vfs.pathFromUri(newUri)
    // directory move: rewrite every child
    const moved: vscode.FileChangeEvent[] = []
    let ok = false
    for (const k of vfs.list()) {
      if (k === o || k.startsWith(o + '/')) {
        const nk = n + k.slice(o.length)
        vfs.files[nk] = vfs.files[k]
        delete vfs.files[k]
        moved.push({ type: vscode.FileChangeType.Deleted, uri: vfs.uri(k) })
        moved.push({ type: vscode.FileChangeType.Created, uri: vfs.uri(nk) })
        ok = true
      }
    }
    if (!ok) throw vscode.FileSystemError.FileNotFound(oldUri)
    vfs.persist()
  }
}
