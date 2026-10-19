// DEVWORLD terminal backend — the real vscode integrated terminal (xterm)
// with our local command set. No shell process exists in the browser, so
// this backend interprets a small command language: help, ls, cat, run,
// echo, exec (backend sandbox), backend (config status), clear.

import {
  ITerminalChildProcess,
  SimpleTerminalBackend,
  SimpleTerminalProcess
} from '@codingame/monaco-vscode-terminal-service-override'
import * as vscode from 'vscode'
import { vfs } from './vfs'
import { getBackend } from './backend'

export function getTerminalBackend(): SimpleTerminalBackend {
  class DevworldTerminalBackend extends SimpleTerminalBackend {
    override getDefaultSystemShell = async (): Promise<string> => 'devworld'
    override createProcess = async (): Promise<ITerminalChildProcess> => {
      const dataEmitter = new vscode.EventEmitter<string>()
      const fire = (s: string) => dataEmitter.fire(s)

      class DevworldProcess extends SimpleTerminalProcess {
        private buf = ''
        private history: string[] = []
        private hIndex = 0

        async start(): Promise<undefined> {
          fire('DEVWORLD terminal — type "help".\r\n$ ')
          return undefined
        }
        override shutdown(_immediate: boolean): void {}
        override clearBuffer(): void | Promise<void> {}
        override sendSignal(): void {}
        override resize(_cols: number, _rows: number): void {}

        override input(data: string): void {
          for (const c of data) {
            const code = c.charCodeAt(0)
            if (code === 13) {
              // enter
              const line = this.buf
              this.buf = ''
              fire('\r\n')
              if (line.trim()) {
                this.history.push(line)
                this.hIndex = this.history.length
              }
              void this.execLine(line).then(() => fire('$ '))
            } else if (code === 127) {
              // backspace
              if (this.buf.length) {
                this.buf = this.buf.slice(0, -1)
                fire('\b \b')
              }
            } else if (code === 3) {
              // ctrl+c
              fire('^C\r\n$ ')
              this.buf = ''
            } else if (c === '\u001b[A') {
              // up: history prev
              if (this.hIndex > 0) {
                this.hIndex--
                fire('\x1b[2K\r$ ' + (this.history[this.hIndex] || ''))
                this.buf = this.history[this.hIndex] || ''
              }
            } else if (c === '\u001b[B') {
              // down: history next
              if (this.hIndex < this.history.length) {
                this.hIndex++
                const h = this.history[this.hIndex] || ''
                fire('\x1b[2K\r$ ' + h)
                this.buf = h
              }
            } else if (code >= 32) {
              this.buf += c
              fire(c)
            }
          }
        }

        private writeln(s: string): void {
          fire(s.replace(/\n/g, '\r\n') + '\r\n')
        }

        private async execLine(line: string): Promise<void> {
          const parts = line.trim().split(/\s+/).filter(Boolean)
          if (!parts.length) return
          const [cmd, ...args] = parts
          try {
            switch (cmd) {
              case 'help': return this.cmdHelp()
              case 'ls': return this.cmdLs(args)
              case 'cat': return this.cmdCat(args)
              case 'echo': return this.writeln(args.join(' '))
              case 'clear': return fire('\x1bc')
              case 'backend': return this.cmdBackend()
              case 'run': return await this.cmdRun(args)
              case 'exec': return await this.cmdExec(args)
              default: return this.writeln(`unknown command: ${cmd} — try "help"`)
            }
          } catch (e: any) {
            this.writeln('error: ' + ((e && e.message) || e))
          }
        }

        private cmdHelp(): void {
          this.writeln('commands:')
          this.writeln('  help              this help')
          this.writeln('  ls [dir]          list files')
          this.writeln('  cat <file>        print a file')
          this.writeln('  run [file]        run a file with the IDE runners')
          this.writeln('  echo <text>       print text')
          this.writeln('  exec <rt> <file>  run on the backend sandbox')
          this.writeln('  backend           show backend config status')
          this.writeln('  clear             clear the terminal')
        }

        private children(dir: string): { files: string[]; dirs: string[] } {
          const files: string[] = []
          const dirs = new Set<string>()
          for (const k of vfs.list()) {
            if (dir && !(k === dir || k.startsWith(dir + '/'))) continue
            const rest = dir ? k.slice(dir.length + 1) : k
            if (!rest) continue
            const seg = rest.split('/')
            if (seg.length === 1) files.push(k)
            else dirs.add(dir ? dir + '/' + seg[0] : seg[0])
          }
          return { files: files.sort(), dirs: [...dirs].sort() }
        }

        private cmdLs(args: string[]): void {
          const dir = (args[0] || '').replace(/^\/+/, '').replace(/\/+$/, '')
          const { files, dirs } = this.children(dir)
          dirs.forEach((d) => this.writeln('\x1b[1;34m' + d.split('/').pop() + '/\x1b[0m'))
          files.forEach((f) => this.writeln(f.split('/').pop()!))
          if (!dirs.length && !files.length) this.writeln('(empty)')
        }

        private cmdCat(args: string[]): void {
          if (!args[0]) return this.writeln('usage: cat <file>')
          const c = vfs.read(args[0])
          if (c === null) this.writeln('no such file: ' + args[0])
          else c.split('\n').forEach((l) => this.writeln(l))
        }

        private cmdBackend(): void {
          const b = getBackend()
          if (b && b.url) this.writeln('backend: ' + b.url)
          else this.writeln('no backend configured. Open Settings (gear icon) and set the backend URL + API key.')
        }

        private async cmdRun(args: string[]): Promise<void> {
          const { runActiveFile } = await import('./gameide')
          const target = args[0]
          if (target && !vfs.exists(target)) return this.writeln('no such file: ' + target)
          await runActiveFile(target)
        }

        private async cmdExec(args: string[]): Promise<void> {
          const b = getBackend()
          if (!b || !b.url) {
            this.writeln('no backend configured — open Settings and set the backend URL + API key first.')
            return
          }
          const [runtime, file] = args
          if (!runtime || !file) return this.writeln('usage: exec <runtime> <file>')
          const content = vfs.read(file)
          if (content === null) return this.writeln('no such file: ' + file)
          this.writeln(`→ POST ${b.url.replace(/\/$/, '')}/execute  runtime=${runtime} file=${file}`)
          try {
            const res = await fetch(b.url.replace(/\/$/, '') + '/execute', {
              method: 'POST',
              headers: Object.assign(
                { 'Content-Type': 'application/json' },
                b.apiKey ? { 'x-api-key': b.apiKey } : {}
              ),
              body: JSON.stringify({ runtime, files: [{ path: file, content }], stdin: '' })
            })
            const text = await res.text()
            this.writeln(`← ${res.status}`)
            text.split('\n').slice(0, 60).forEach((l) => this.writeln(l))
          } catch (e: any) {
            this.writeln('request failed: ' + ((e && e.message) || e))
          }
        }
      }
      return new DevworldProcess(1, 1, '/workspace', dataEmitter.event)
    }
  }
  return new DevworldTerminalBackend()
}
