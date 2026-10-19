/**
 * Container runner: executes untrusted code in a fresh, locked-down
 * Docker container per run.
 *
 * Isolation per run:
 *  - --network none            no network access
 *  - --read-only               read-only root filesystem
 *  - -v <tmp>:/work:rw         only /work is writable (submission files live here)
 *  - --tmpfs /tmp:rw,size=64m,noexec   small writable /tmp for compilers
 *  - --cap-drop ALL + --security-opt no-new-privileges=true
 *  - --pids-limit, --memory (+ matching --memory-swap), --cpus
 *  - --init                    zombie reaping
 *  - wall-clock timeout kills the container (docker kill)
 *  - stdout/stderr truncated to 256 KB each while still draining the pipes
 *    (so a noisy process cannot deadlock on a full pipe)
 *  - files written to /work/output are collected as base64 artifacts (5 MB cap)
 *
 * NOTE on bind mounts: the worker talks to the host Docker daemon through
 * /var/run/docker.sock, so the submissions dir is resolved ON THE HOST.
 * It must therefore be mounted at the SAME absolute path inside the worker
 * container (see docker-compose.yml: /submissions).
 */

import { mkdtemp, mkdir, writeFile, readdir, readFile, rm, lstat } from "node:fs/promises";
import { join, relative } from "node:path";
import { tmpdir } from "node:os";
import { randomUUID } from "node:crypto";
import type { FileInput } from "./validate";

export interface RunOptions {
  image: string;
  files: FileInput[];
  entry: string; // entry file path relative to /work
  buildCommand: string | null;
  runCommand: string;
  stdin: string;
  timeoutSec: number;
  memory: string;
  cpus: string;
  pidsLimit: number;
  network: string;
  /** Extra -e KEY=VALUE pairs, e.g. GOCACHE for Go. */
  env?: Record<string, string>;
}

export interface Artifact {
  path: string; // relative to /work/output
  base64: string;
}

export interface RunResult {
  stdout: string;
  stderr: string;
  exitCode: number;
  timedOut: boolean;
  /** true if stdout or stderr exceeded the truncation cap. */
  truncated: boolean;
  durationMs: number;
  artifacts: Artifact[];
}

export interface ContainerRunner {
  run(opts: RunOptions): Promise<RunResult>;
}

export class RunnerError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "RunnerError";
  }
}

const STREAM_CAP = 256 * 1024; // per stream
const ARTIFACT_TOTAL_CAP = 5 * 1024 * 1024;

export function renderCommand(template: string, entry: string): string {
  const entryBase = entry.includes(".") ? entry.slice(0, entry.lastIndexOf(".")) : entry;
  return template
    .replaceAll("{entry}", entry)
    .replaceAll("{entryBase}", entryBase)
    .replaceAll("{stdinFile}", "/work/__stdin.txt");
}

async function drainWithCap(
  stream: ReadableStream<Uint8Array> | null,
  cap: number,
): Promise<{ text: string; truncated: boolean }> {
  if (!stream) return { text: "", truncated: false };
  const reader = stream.getReader();
  const decoder = new TextDecoder();
  let text = "";
  let kept = 0;
  let truncated = false;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (kept < cap) {
        const chunk = decoder.decode(value, { stream: true });
        const room = cap - kept;
        if (chunk.length <= room) {
          text += chunk;
          kept += chunk.length;
        } else {
          text += chunk.slice(0, room);
          kept = cap;
          truncated = true;
        }
      } else {
        truncated = true; // keep draining so the child never blocks on a full pipe
      }
    }
    const tail = decoder.decode();
    if (tail && kept < cap) {
      const room = cap - kept;
      text += tail.slice(0, room);
      if (tail.length > room) truncated = true;
    } else if (tail) {
      truncated = true;
    }
  } finally {
    reader.releaseLock();
  }
  return { text, truncated };
}

async function collectArtifacts(outputDir: string): Promise<Artifact[]> {
  const artifacts: Artifact[] = [];
  let total = 0;
  async function walk(dir: string): Promise<void> {
    let entries;
    try {
      entries = await readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (total >= ARTIFACT_TOTAL_CAP) return;
      const full = join(dir, e.name);
      if (e.isSymbolicLink()) continue; // never follow symlinks out of /work/output
      if (e.isDirectory()) {
        await walk(full);
      } else if (e.isFile()) {
        const st = await lstat(full);
        const remaining = ARTIFACT_TOTAL_CAP - total;
        if (st.size > remaining) continue; // skip files that would blow the cap
        const data = await readFile(full);
        total += data.byteLength;
        artifacts.push({ path: relative(outputDir, full), base64: data.toString("base64") });
      }
    }
  }
  await walk(outputDir);
  return artifacts;
}

export interface DockerCliRunnerOptions {
  /** Absolute path shared 1:1 between host and worker container. */
  submissionsDir: string;
  dockerBin?: string;
}

export class DockerCliRunner implements ContainerRunner {
  private readonly submissionsDir: string;
  private readonly dockerBin: string;

  constructor(opts: DockerCliRunnerOptions) {
    this.submissionsDir = opts.submissionsDir;
    this.dockerBin = opts.dockerBin ?? "docker";
  }

  async run(opts: RunOptions): Promise<RunResult> {
    const startedAt = Date.now();
    const id = `exec-${randomUUID().replace(/-/g, "").slice(0, 12)}`;
    const hostDir = join(this.submissionsDir, id);
    await mkdir(join(hostDir, "output"), { recursive: true });

    const name = `devworld-${id}`;
    let timedOut = false;
    try {
      for (const f of opts.files) {
        const dest = join(hostDir, f.path);
        await mkdir(join(dest, ".."), { recursive: true });
        await writeFile(dest, f.content);
      }
      await writeFile(join(hostDir, "__stdin.txt"), opts.stdin);

      const inner = opts.buildCommand
        ? `${renderCommand(opts.buildCommand, opts.entry)} && ${renderCommand(opts.runCommand, opts.entry)}`
        : renderCommand(opts.runCommand, opts.entry);
      // Redirect to files so stdout/stderr stay separable without relying on
      // docker's multiplexed attach stream; results are copied back afterwards.
      const guestScript = `cd /work && ( ${inner} ) >/work/__stdout.txt 2>/work/__stderr.txt; echo $? >/work/__exit.txt`;

      const args = [
        "run",
        "--rm",
        "--init",
        "--name",
        name,
        "--network",
        opts.network,
        "--memory",
        opts.memory,
        "--memory-swap",
        opts.memory,
        "--cpus",
        opts.cpus,
        "--pids-limit",
        String(opts.pidsLimit),
        "--read-only",
        "--cap-drop",
        "ALL",
        "--security-opt",
        "no-new-privileges=true",
        "--tmpfs",
        "/tmp:rw,size=64m,noexec",
        "-v",
        `${hostDir}:/work:rw`,
        "-i",
      ];
      for (const [k, v] of Object.entries(opts.env ?? {})) {
        args.push("-e", `${k}=${v}`);
      }
      args.push(opts.image, "sh", "-c", guestScript);

      const proc = Bun.spawn([this.dockerBin, ...args], {
        stdin: opts.stdin ? Buffer.from(opts.stdin, "utf8") : "ignore",
        stdout: "pipe",
        stderr: "pipe",
      });

      const stdoutP = drainWithCap(proc.stdout, STREAM_CAP);
      const stderrP = drainWithCap(proc.stderr, STREAM_CAP);

      const killTimer = setTimeout(() => {
        timedOut = true;
        const killer = Bun.spawn([this.dockerBin, "kill", name], {
          stdout: "ignore",
          stderr: "ignore",
        });
        killer.exited.catch(() => {});
      }, opts.timeoutSec * 1000);

      let exitCode: number;
      try {
        exitCode = await proc.exited;
      } finally {
        clearTimeout(killTimer);
      }
      const [so, se] = await Promise.all([stdoutP, stderrP]);

      // Container stdout/stderr carry docker-level messages only; the real
      // program output was redirected to files inside /work.
      let stdout = "";
      let stderr = "";
      let programExit = exitCode;
      try {
        stdout = await readFile(join(hostDir, "__stdout.txt"), "utf8");
      } catch {
        /* container died before writing output */
      }
      try {
        stderr = await readFile(join(hostDir, "__stderr.txt"), "utf8");
      } catch {
        /* same */
      }
      try {
        const raw = await readFile(join(hostDir, "__exit.txt"), "utf8");
        const parsed = Number.parseInt(raw.trim(), 10);
        if (Number.isInteger(parsed)) programExit = parsed;
      } catch {
        /* fall back to docker exit code */
      }

      const capNote = "\n[truncated: output exceeded 256KB]";
      if (stdout.length > STREAM_CAP) {
        stdout = stdout.slice(0, STREAM_CAP) + capNote;
      }
      if (stderr.length > STREAM_CAP) {
        stderr = stderr.slice(0, STREAM_CAP) + capNote;
      }
      const truncated =
        so.truncated || se.truncated || stdout.endsWith(capNote) || stderr.endsWith(capNote);

      // Surface docker-level failures (image missing, daemon errors) in stderr
      // so they are visible instead of silently swallowed.
      const dockerNote = (so.text + se.text).trim();
      if (dockerNote && (!stdout || timedOut)) {
        stderr = (stderr ? stderr + "\n" : "") + `[docker] ${dockerNote.slice(0, 2000)}`;
      }

      const artifacts = await collectArtifacts(join(hostDir, "output"));

      return {
        stdout,
        stderr,
        exitCode: programExit,
        timedOut,
        truncated,
        durationMs: Date.now() - startedAt,
        artifacts,
      };
    } catch (err) {
      // Best effort: make sure a stray container is not left behind.
      try {
        const killer = Bun.spawn([this.dockerBin, "rm", "-f", name], {
          stdout: "ignore",
          stderr: "ignore",
        });
        await killer.exited;
      } catch {
        /* ignore */
      }
      throw new RunnerError(
        `container execution failed: ${err instanceof Error ? err.message : String(err)}`,
      );
    } finally {
      await rm(hostDir, { recursive: true, force: true }).catch(() => {});
    }
  }
}

/**
 * In-process fake for tests: no Docker needed.
 */
export class FakeRunner implements ContainerRunner {
  constructor(private readonly handler: (opts: RunOptions) => RunResult | Promise<RunResult>) {}

  run(opts: RunOptions): Promise<RunResult> {
    return Promise.resolve(this.handler(opts));
  }
}

export function echoRunner(): FakeRunner {
  return new FakeRunner((opts) => ({
    stdout: opts.stdin,
    stderr: "",
    exitCode: 0,
    timedOut: false,
    truncated: false,
    durationMs: 1,
    artifacts: [],
  }));
}
