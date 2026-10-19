/* DEVWORLD IDE — terminal (xterm.js), local commands, backend exec stub.
   Backend contract (owned by the execution-worker agent):
     POST {url}/execute  header x-api-key: {apiKey}
     body { runtime, files: {path: content}, stdin }
   Configure via window.GameIDE.setBackend({url, apiKey}) — persisted to localStorage. */

const BACKEND_KEY = 'devworld.backend.v1';

const Term = {
  term: null,
  buf: '',
  history: [],
  hIndex: -1,
  ready: false,

  backend() {
    try { return JSON.parse(localStorage.getItem(BACKEND_KEY) || 'null'); } catch (e) { return null; }
  },

  init() {
    if (this.ready) return;
    if (typeof Terminal === 'undefined') {
      document.getElementById('termwrap').innerHTML =
        '<p class="subtle" style="padding:8px">Terminal library failed to load (CDN). Check your connection and reload.</p>';
      return;
    }
    this.term = new Terminal({
      theme: { background: '#0d1117', foreground: '#e6edf3', cursor: '#1f6feb' },
      fontSize: 13,
      cols: 100,
      rows: 12,
    });
    this.term.open(document.getElementById('termwrap'));
    this.ready = true;
    this.writeln('DEVWORLD terminal — type "help".');
    this.prompt();
    this.term.onData((d) => this.onData(d));
  },

  writeln(s) { if (this.ready) this.term.writeln(s); },
  write(s) { if (this.ready) this.term.write(s); },
  prompt() { this.write('\x1b[1;34mdevworld\x1b[0m $ '); },

  onData(d) {
    const t = this.term;
    if (d === '\r') { // enter
      t.writeln('');
      const cmd = this.buf;
      this.buf = '';
      if (cmd.trim()) { this.history.push(cmd); this.hIndex = this.history.length; }
      this.execLine(cmd).finally(() => this.prompt());
    } else if (d === '\u007f') { // backspace
      if (this.buf.length) { this.buf = this.buf.slice(0, -1); t.write('\b \b'); }
    } else if (d === '\u001b[A') { // up
      if (this.hIndex > 0) {
        this.hIndex--;
        t.write('\x1b[2K\r'); this.prompt();
        this.buf = this.history[this.hIndex] || '';
        t.write(this.buf);
      }
    } else if (d === '\u001b[B') { // down
      if (this.hIndex < this.history.length) {
        this.hIndex++;
        t.write('\x1b[2K\r'); this.prompt();
        this.buf = this.history[this.hIndex] || '';
        t.write(this.buf);
      }
    } else if (d === '\u0003') { // ctrl+c
      t.writeln('^C');
      this.buf = '';
      this.prompt();
    } else if (d >= ' ' || d === '\t') {
      this.buf += d;
      t.write(d);
    }
  },

  async execLine(line) {
    const parts = line.trim().split(/\s+/).filter(Boolean);
    if (!parts.length) return;
    const [cmd, ...args] = parts;
    const c = this.commands[cmd];
    if (!c) { this.writeln(`unknown command: ${cmd} — try "help"`); return; }
    try { await c.call(this, args); }
    catch (e) { this.writeln('error: ' + (e && e.message || e)); }
  },

  commands: {
    help() {
      this.writeln('commands:');
      this.writeln('  help              this help');
      this.writeln('  ls [dir]          list files');
      this.writeln('  cat <file>        print a file');
      this.writeln('  run [file]        run a file with the IDE runners');
      this.writeln('  echo <text>       print text');
      this.writeln('  exec <rt> <file>  run on the backend sandbox (needs setBackend)');
      this.writeln('  backend           show backend config status');
      this.writeln('  clear             clear the terminal');
    },
    ls(args) {
      const dir = VFS.norm(args[0] || '');
      const { files, dirs } = VFS.children(dir);
      dirs.forEach((d) => this.writeln('\x1b[1;34m' + d.split('/').pop() + '/\x1b[0m'));
      files.forEach((f) => this.writeln(f.split('/').pop()));
      if (!dirs.length && !files.length) this.writeln('(empty)');
    },
    cat(args) {
      if (!args[0]) { this.writeln('usage: cat <file>'); return; }
      const c = VFS.read(args[0]);
      if (c === null) this.writeln('no such file: ' + args[0]);
      else c.split('\n').forEach((l) => this.writeln(l));
    },
    async run(args) {
      const p = args[0] ? VFS.norm(args[0]) : Tabs.active();
      if (!p || !VFS.exists(p)) { this.writeln('no such file: ' + (args[0] || '(none open)')); return; }
      showBottom('output');
      await runFile(p);
    },
    echo(args) { this.writeln(args.join(' ')); },
    clear() { this.term.clear(); },
    backend() {
      const b = this.backend();
      if (b && b.url) this.writeln('backend: ' + b.url + (b.apiKey ? ' (key set)' : ' (no key)'));
      else this.writeln('no backend configured. Open Settings (gear icon) and set the backend URL + API key,\nor run: GameIDE.setBackend({url: "https://…", apiKey: "…"})');
    },
    async exec(args) {
      // POST {url}/execute {runtime, files: [{path, content}], stdin}
      const b = this.backend();
      if (!b || !b.url) {
        this.writeln('no backend configured — open Settings and set the backend URL + API key first.');
        return;
      }
      const [runtime, file] = args;
      if (!runtime || !file) { this.writeln('usage: exec <runtime> <file>'); return; }
      const p = VFS.norm(file);
      const content = VFS.read(p);
      if (content === null) { this.writeln('no such file: ' + file); return; }
      this.writeln(`→ POST ${b.url.replace(/\/$/, '')}/execute  runtime=${runtime} file=${p}`);
      try {
        const res = await fetch(b.url.replace(/\/$/, '') + '/execute', {
          method: 'POST',
          headers: Object.assign(
            { 'Content-Type': 'application/json' },
            b.apiKey ? { 'x-api-key': b.apiKey } : {}
          ),
          body: JSON.stringify({ runtime, files: [{ path: p, content }], stdin: '' }),
        });
        const text = await res.text();
        this.writeln(`← ${res.status}`);
        text.split('\n').slice(0, 60).forEach((l) => this.writeln(l));
      } catch (e) {
        this.writeln('request failed: ' + (e && e.message || e));
      }
    },
  },
};

function showBottom(name) {
  document.querySelectorAll('.bptab').forEach((b) =>
    b.classList.toggle('active', b.dataset.bp === name));
  document.querySelectorAll('.bppane').forEach((p) =>
    p.classList.toggle('active', p.id === 'bp-' + name));
  if (name === 'terminal') Term.init();
}
