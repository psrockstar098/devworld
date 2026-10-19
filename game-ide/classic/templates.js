/* DEVWORLD IDE — "New Project" dialog with multi-file templates. */

const TEMPLATES = [
  {
    id: 'python-script',
    name: 'Python script',
    desc: 'A single Python program with a main entry point.',
    files: {
      'main.py': 'def main():\n    print("Hello, DevWorld!")\n\n\nif __name__ == "__main__":\n    main()\n',
      'README.md': '# Python project\n\nRun `main.py` with the Run button.\n',
    },
    open: 'main.py',
  },
  {
    id: 'web-page',
    name: 'Web page (HTML/CSS/JS)',
    desc: 'A small interactive web page. (Previewing it in a browser tab comes later — the files are editable now.)',
    files: {
      'index.html': '<!DOCTYPE html>\n<html>\n<head>\n<meta charset="UTF-8">\n<title>My page</title>\n<link rel="stylesheet" href="styles.css">\n</head>\n<body>\n<h1 id="title">Hello, DevWorld!</h1>\n<button id="btn">Click me</button>\n<script src="app.js"></script>\n</body>\n</html>\n',
      'styles.css': 'body {\n  font-family: sans-serif;\n  background: #0d1117;\n  color: #e6edf3;\n  text-align: center;\n  padding-top: 60px;\n}\nbutton {\n  padding: 10px 20px;\n  font-size: 16px;\n  cursor: pointer;\n}\n',
      'app.js': 'let clicks = 0;\ndocument.getElementById("btn").addEventListener("click", () => {\n  clicks++;\n  document.getElementById("title").textContent =\n    `Clicked ${clicks} time${clicks === 1 ? "" : "s"}!`;\n});\n',
    },
    open: 'index.html',
  },
  {
    id: 'c-program',
    name: 'C program',
    desc: 'A C starter. Compiles and runs on the game sandbox (later) — edit it here today.',
    files: {
      'main.c': '#include <stdio.h>\n\nint main(void) {\n    printf("Hello, DevWorld!\\n");\n    return 0;\n}\n',
      'README.md': '# C project\n\nBuild (later, on the sandbox):\n\n    gcc -O2 -o game main.c\n    ./game\n',
    },
    open: 'main.c',
  },
  {
    id: 'node-cli',
    name: 'Node CLI',
    desc: 'A command-line tool in JavaScript. Runs locally in the IDE today.',
    files: {
      'package.json': '{\n  "name": "my-cli",\n  "version": "1.0.0",\n  "type": "module",\n  "bin": { "my-cli": "./index.js" }\n}\n',
      'index.js': '#!/usr/bin/env node\nconst [,, ...args] = process.argv;\n\nif (args.length === 0) {\n  console.log("usage: my-cli <name>");\n  process.exit(1);\n}\nconsole.log(`Hello, ${args[0]}!`);\n',
      'README.md': '# Node CLI\n\nRun with: `node index.js World`\n',
    },
    open: 'index.js',
  },
  {
    id: 'sql-playground',
    name: 'SQL playground',
    desc: 'Schema plus queries against a throwaway SQLite database. Runs locally today.',
    files: {
      'schema.sql': 'CREATE TABLE players (\n  id INTEGER PRIMARY KEY,\n  name TEXT NOT NULL,\n  hp INTEGER DEFAULT 100\n);\n\nINSERT INTO players (name, hp) VALUES\n  (\'dev\', 100),\n  (\'nova\', 75),\n  (\'rex\', 20);\n',
      'queries.sql': '-- strongest first\nSELECT name, hp FROM players ORDER BY hp DESC;\n\n-- average hp\nSELECT AVG(hp) AS avg_hp FROM players;\n',
      'README.md': '# SQL playground\n\n`schema.sql` builds the database, `queries.sql` asks questions.\nRun either file with the Run button.\n',
    },
    open: 'schema.sql',
  },
];

const TemplatesUI = {
  dialog() {
    const modal = document.getElementById('modal');
    const box = document.getElementById('modalBox');
    box.innerHTML = '<h3>New Project</h3><p class="subtle" style="margin-bottom:10px">Pick a template — files land in a new folder.</p>';
    TEMPLATES.forEach((t) => {
      const d = document.createElement('div');
      d.className = 'tpl';
      d.innerHTML = `<b></b><p></p><div class="files"></div>`;
      d.querySelector('b').textContent = t.name;
      d.querySelector('p').textContent = t.desc;
      d.querySelector('.files').textContent = Object.keys(t.files).join('   ');
      d.addEventListener('click', () => {
        modal.classList.add('hidden');
        this.scaffold(t);
      });
      box.appendChild(d);
    });
    const cancel = document.createElement('button');
    cancel.className = 'ghost wide';
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', () => modal.classList.add('hidden'));
    box.appendChild(cancel);
    modal.classList.remove('hidden');
  },

  scaffold(t) {
    // unique folder name
    let base = t.id, folder = base, n = 1;
    while (VFS.dirs().includes(folder) || VFS.list().some((p) => p === folder || p.startsWith(folder + '/'))) {
      n++;
      folder = `${base}-${n}`;
    }
    Object.keys(t.files).forEach((rel) => {
      VFS.write(`${folder}/${rel}`, t.files[rel]);
    });
    Explorer.render();
    Tabs.openFile(`${folder}/${t.open}`);
  },
};
