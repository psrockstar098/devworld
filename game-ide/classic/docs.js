/* DEVWORLD IDE — static language cheat sheets. Rendered in the Docs panel,
   filterable. Kept short: quick syntax reminders, not a tutorial. */

const DOCS = {
  python: {
    title: 'Python 3.14',
    sections: [
      { h: 'Hello + variables', code: 'print("hello")\nname = "dev"\nscore = 42\npi = 3.14' },
      { h: 'Input', code: 'name = input("name? ")\nn = int(input())' },
      { h: 'Conditionals', code: 'if score >= 90:\n    print("A")\nelif score >= 50:\n    print("pass")\nelse:\n    print("fail")' },
      { h: 'Loops', code: 'for i in range(5):\n    print(i)\n\nwhile n > 0:\n    n -= 1' },
      { h: 'Functions', code: 'def add(a, b):\n    return a + b\n\nprint(add(2, 3))' },
      { h: 'Lists / dicts', code: 'xs = [1, 2, 3]\nxs.append(4)\nprint(xs[0], len(xs))\n\nd = {"hp": 100}\nprint(d["hp"])' },
      { h: 'Strings', code: 's = f"hp is {100 - 25}"\nprint(s.upper(), s.split())' },
      { h: 'Exceptions', code: 'try:\n    x = int("abc")\nexcept ValueError:\n    print("not a number")' },
    ],
  },
  javascript: {
    title: 'JavaScript (ES2020)',
    sections: [
      { h: 'Hello + variables', code: 'console.log("hello");\nlet hp = 100;\nconst NAME = "dev";' },
      { h: 'Input (in tests)', code: '// with stdin wired, use:\nconst name = prompt();\nconst line = readline();' },
      { h: 'Conditionals', code: 'if (hp > 50) {\n  console.log("healthy");\n} else {\n  console.log("hurt");\n}' },
      { h: 'Loops', code: 'for (let i = 0; i < 5; i++) {\n  console.log(i);\n}\nlet n = 3;\nwhile (n--) console.log(n);' },
      { h: 'Functions', code: 'function add(a, b) { return a + b; }\nconst mul = (a, b) => a * b;\nconsole.log(add(2, 3));' },
      { h: 'Arrays / objects', code: 'const xs = [1, 2, 3];\nxs.push(4);\nconsole.log(xs.map(x => x * 2));\n\nconst p = { hp: 100 };\nconsole.log(p.hp);' },
      { h: 'Strings', code: 'const s = `hp is ${100 - 25}`;\nconsole.log(s.toUpperCase().split(" "));' },
      { h: 'Errors', code: 'try {\n  throw new Error("boom");\n} catch (e) {\n  console.log("caught:", e.message);\n}' },
    ],
  },
  typescript: {
    title: 'TypeScript 5.9',
    sections: [
      { h: 'Typed variables', code: 'let hp: number = 100;\nconst name: string = "dev";\nlet alive: boolean = true;' },
      { h: 'Functions', code: 'function add(a: number, b: number): number {\n  return a + b;\n}' },
      { h: 'Interfaces', code: 'interface Player {\n  name: string;\n  hp: number;\n}\nconst p: Player = { name: "dev", hp: 100 };' },
      { h: 'Unions / optional', code: 'let id: number | string = 7;\nfunction heal(p?: Player) { /* ... */ }' },
      { h: 'Generics', code: 'function first<T>(xs: T[]): T {\n  return xs[0];\n}' },
    ],
  },
  sql: {
    title: 'SQL (SQLite)',
    sections: [
      { h: 'Create + insert', code: 'CREATE TABLE players (id INTEGER PRIMARY KEY, name TEXT, hp INTEGER);\nINSERT INTO players (name, hp) VALUES (\'dev\', 100);' },
      { h: 'Select', code: 'SELECT * FROM players;\nSELECT name, hp FROM players WHERE hp > 50 ORDER BY hp DESC;' },
      { h: 'Update / delete', code: 'UPDATE players SET hp = 75 WHERE name = \'dev\';\nDELETE FROM players WHERE hp <= 0;' },
      { h: 'Aggregates', code: 'SELECT COUNT(*), AVG(hp), MAX(hp) FROM players;' },
      { h: 'Join', code: 'SELECT p.name, s.title FROM players p\nJOIN scores s ON s.player_id = p.id;' },
    ],
  },
  lua: {
    title: 'Lua 5.3',
    sections: [
      { h: 'Hello + variables', code: 'print("hello")\nlocal hp = 100\nname = "dev"  -- global' },
      { h: 'Conditionals', code: 'if hp > 50 then\n  print("healthy")\nelse\n  print("hurt")\nend' },
      { h: 'Loops', code: 'for i = 1, 5 do\n  print(i)\nend\n\nlocal n = 3\nwhile n > 0 do\n  n = n - 1\nend' },
      { h: 'Functions', code: 'local function add(a, b)\n  return a + b\nend\nprint(add(2, 3))' },
      { h: 'Tables', code: 'local p = { name = "dev", hp = 100 }\nprint(p.name)\n\nlocal xs = { 1, 2, 3 }\ntable.insert(xs, 4)\nprint(#xs, xs[1])' },
      { h: 'Strings', code: 'local s = "hp is " .. (100 - 25)\nprint(string.upper(s))' },
    ],
  },
  c: {
    title: 'C (runs on sandbox later)',
    sections: [
      { h: 'Hello', code: '#include <stdio.h>\nint main(void) {\n    printf("hello\\n");\n    return 0;\n}' },
      { h: 'Variables', code: 'int hp = 100;\nfloat pi = 3.14f;\nchar name[] = "dev";' },
      { h: 'Input', code: 'int n;\nscanf("%d", &n);' },
      { h: 'Loops', code: 'for (int i = 0; i < 5; i++) {\n    printf("%d\\n", i);\n}' },
      { h: 'Functions', code: 'int add(int a, int b) {\n    return a + b;\n}' },
    ],
  },
  rust: {
    title: 'Rust (runs on sandbox later)',
    sections: [
      { h: 'Hello', code: 'fn main() {\n    println!("hello");\n}' },
      { h: 'Variables', code: 'let hp = 100;\nlet mut score = 0;\nscore += 10;' },
      { h: 'Conditionals', code: 'if hp > 50 {\n    println!("healthy");\n} else {\n    println!("hurt");\n}' },
      { h: 'Loops', code: 'for i in 0..5 {\n    println!("{}", i);\n}' },
      { h: 'Functions', code: 'fn add(a: i32, b: i32) -> i32 {\n    a + b\n}' },
      { h: 'Ownership note', code: 'let s = String::from("dev");\nlet t = s; // s moved here\n// println!("{}", s); // error!' },
    ],
  },
  go: {
    title: 'Go (runs on sandbox later)',
    sections: [
      { h: 'Hello', code: 'package main\n\nimport "fmt"\n\nfunc main() {\n    fmt.Println("hello")\n}' },
      { h: 'Variables', code: 'hp := 100\nvar name string = "dev"' },
      { h: 'Conditionals', code: 'if hp > 50 {\n    fmt.Println("healthy")\n}' },
      { h: 'Loops', code: 'for i := 0; i < 5; i++ {\n    fmt.Println(i)\n}' },
      { h: 'Functions', code: 'func add(a, b int) int {\n    return a + b\n}' },
    ],
  },
};

const DocsUI = {
  render(filter) {
    const host = document.getElementById('docsList');
    host.innerHTML = '';
    const q = (filter || '').trim().toLowerCase();
    Object.keys(DOCS).forEach((id) => {
      const doc = DOCS[id];
      const secs = doc.sections.filter((s) =>
        !q || s.h.toLowerCase().includes(q) || s.code.toLowerCase().includes(q) || doc.title.toLowerCase().includes(q));
      if (!secs.length) return;
      const lang = document.createElement('div');
      lang.className = 'doclang';
      lang.textContent = doc.title.toUpperCase();
      host.appendChild(lang);
      secs.forEach((s) => {
        const d = document.createElement('div');
        d.className = 'docsec';
        const h = document.createElement('h4');
        h.textContent = s.h;
        const pre = document.createElement('pre');
        pre.textContent = s.code;
        d.appendChild(h); d.appendChild(pre);
        host.appendChild(d);
      });
    });
    if (!host.children.length) host.innerHTML = '<p class="subtle">No matches.</p>';
  },
};
