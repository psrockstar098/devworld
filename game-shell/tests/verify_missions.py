#!/usr/bin/env python3
"""Verify every mission against a reference solution.

Why this exists: the C++ suite proves content is structurally valid, but it
cannot prove the hidden-test expectations are *achievable*. This script runs
each mission the way the in-game IDE does (stdin -> stdout, trimmed compare):

  * the shipped starter must FAIL at least one test (there is real work to do)
  * the reference solution in tests/solutions/ must PASS every test

Reference solutions never ship with the game: the game preloads only content/.
SQL is executed with SQLite and formatted like the IDE's sql.js runner
(columns, then rows, cells joined with " | ").

Usage: python3 tests/verify_missions.py [--only m13]
"""
import json
import pathlib
import sqlite3
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
GAME = ROOT.parent
MISSIONS = GAME / "content" / "missions.json"
SOLUTIONS = ROOT / "solutions"


def run_python(code: str, stdin: str) -> str:
    r = subprocess.run(
        [sys.executable, "-c", code],
        input=stdin, capture_output=True, text=True, timeout=20,
    )
    return r.stdout


def run_sql(code: str, _stdin: str) -> str:
    """Mirror vscode-ide/src/runners.ts captureSql: result sets only."""
    conn = sqlite3.connect(":memory:")
    out = []
    buf = ""
    try:
        for line in code.splitlines(keepends=True):
            buf += line
            if sqlite3.complete_statement(buf):
                stmt = buf.strip()
                buf = ""
                if not stmt:
                    continue
                cur = conn.execute(stmt)
                if cur.description:
                    out.append(" | ".join(d[0] for d in cur.description))
                    for row in cur.fetchall():
                        out.append(" | ".join("" if v is None else str(v) for v in row))
        if buf.strip():
            cur = conn.execute(buf)
            if cur.description:
                out.append(" | ".join(d[0] for d in cur.description))
                for row in cur.fetchall():
                    out.append(" | ".join("" if v is None else str(v) for v in row))
        return "\n".join(out)
    except sqlite3.Error as e:
        return f"Error: {e}\n"
    finally:
        conn.close()


RUNNERS = {"python": run_python, "sql": run_sql}


def check(label: str, code: str, lang: str, cases, want_all: bool):
    run = RUNNERS[lang]
    failures = []
    for tc in cases:
        try:
            actual = run(code, tc.get("stdin", ""))
        except subprocess.TimeoutExpired:
            actual = ""
        if actual.strip() != tc["expected"].strip():
            failures.append(tc["name"])
    passed = len(cases) - len(failures)
    ok = passed == len(cases) if want_all else passed < len(cases)
    return ok, passed, len(cases), failures


def main() -> int:
    data = json.loads(MISSIONS.read_text())
    only = sys.argv[2] if len(sys.argv) >= 3 and sys.argv[1] == "--only" else None
    failed = 0

    for m in data["missions"]:
        mid = m["id"]
        if only and mid != only:
            continue
        lang = m["language"]
        cases = m["tests"]
        sol = SOLUTIONS / f"{mid}.{'py' if lang == 'python' else 'sql'}"
        problems = []

        # The starter is the player's starting point: it must not already pass.
        ok, passed, total, names = check("starter", m["starter"], lang, cases, False)
        if not ok:
            problems.append(f"starter already passes all {total} tests")

        if not sol.exists():
            problems.append(f"missing reference solution {sol.relative_to(GAME)}")
        else:
            ok, passed, total, names = check("solution", sol.read_text(), lang, cases, True)
            if not ok:
                problems.append(f"solution fails {passed}/{total}: {', '.join(names)}")

        if problems:
            failed += 1
            print(f"FAIL {mid} — {'; '.join(problems)}")
        else:
            print(f"ok   {mid}  starter fails as intended, solution passes {len(cases)}/{len(cases)}")

    print(f"\n{failed} mission(s) failed verification")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
