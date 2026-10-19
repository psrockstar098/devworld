import { getRuntime } from "./runtimes";

export interface FileInput {
  path: string;
  content: string;
}

export interface TestInput {
  name: string;
  stdin: string;
  expectedStdout: string;
  hidden: boolean;
}

export interface ValidatedExecute {
  runtimeId: string;
  files: FileInput[];
  stdin: string;
  timeoutSec: number;
}

export interface ValidatedAssess extends ValidatedExecute {
  tests: TestInput[];
}

export class ValidationError extends Error {
  readonly status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "ValidationError";
    this.status = status;
  }
}

const MAX_FILES = 20;
const MAX_FILE_BYTES = 200 * 1024; // per file
const MAX_STDIN_BYTES = 64 * 1024;
const MAX_OUTPUT_BYTES = 64 * 1024; // expected_stdout per test
const MAX_TESTS = 20;
const MAX_TEST_NAME = 100;
const MAX_TIMEOUT_SEC = 120;
const RESERVED_PREFIX = "__";

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function validateRuntimeId(v: unknown): string {
  if (typeof v !== "string" || !/^[a-z0-9]+$/.test(v)) {
    throw new ValidationError(400, "runtime must be a lowercase alphanumeric id");
  }
  if (!getRuntime(v)) {
    throw new ValidationError(400, `unknown runtime: ${v}`);
  }
  return v;
}

function validatePath(raw: string): string {
  if (raw.length === 0 || raw.length > 200) {
    throw new ValidationError(400, "file path must be 1-200 characters");
  }
  if (raw.startsWith("/") || raw.includes("\\") || raw === "." || raw === "..") {
    throw new ValidationError(400, `invalid file path: ${raw}`);
  }
  const parts = raw.split("/");
  if (parts.some((p) => p === "" || p === "." || p === "..")) {
    throw new ValidationError(400, `invalid file path: ${raw}`);
  }
  const normalized = parts.join("/");
  const base = parts[parts.length - 1];
  if (base.startsWith(RESERVED_PREFIX)) {
    throw new ValidationError(400, `file name prefix "${RESERVED_PREFIX}" is reserved: ${raw}`);
  }
  return normalized;
}

function validateFiles(v: unknown, maxTotalBytes: number): FileInput[] {
  if (!Array.isArray(v) || v.length === 0) {
    throw new ValidationError(400, "files must be a non-empty array");
  }
  if (v.length > MAX_FILES) {
    throw new ValidationError(400, `at most ${MAX_FILES} files per submission`);
  }
  const seen = new Set<string>();
  const files: FileInput[] = [];
  let total = 0;
  for (const item of v) {
    if (!isRecord(item) || typeof item.path !== "string" || typeof item.content !== "string") {
      throw new ValidationError(400, "each file needs a string path and string content");
    }
    const path = validatePath(item.path);
    if (seen.has(path)) {
      throw new ValidationError(400, `duplicate file path: ${path}`);
    }
    seen.add(path);
    const bytes = Buffer.byteLength(item.content, "utf8");
    if (bytes > MAX_FILE_BYTES) {
      throw new ValidationError(413, `file too large: ${path} (max ${MAX_FILE_BYTES} bytes)`);
    }
    total += bytes;
    files.push({ path, content: item.content });
  }
  if (total > maxTotalBytes) {
    throw new ValidationError(413, `submission too large (max ${maxTotalBytes} bytes total)`);
  }
  return files;
}

function validateStdin(v: unknown): string {
  if (v === undefined || v === null) return "";
  if (typeof v !== "string") throw new ValidationError(400, "stdin must be a string");
  if (Buffer.byteLength(v, "utf8") > MAX_STDIN_BYTES) {
    throw new ValidationError(413, `stdin too large (max ${MAX_STDIN_BYTES} bytes)`);
  }
  return v;
}

function validateTimeout(v: unknown): number | undefined {
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "number" || !Number.isFinite(v)) {
    throw new ValidationError(400, "timeout must be a number of seconds");
  }
  if (v < 1 || v > MAX_TIMEOUT_SEC) {
    throw new ValidationError(400, `timeout must be between 1 and ${MAX_TIMEOUT_SEC} seconds`);
  }
  return Math.ceil(v);
}

function validateTests(v: unknown): TestInput[] {
  if (!Array.isArray(v) || v.length === 0) {
    throw new ValidationError(400, "tests must be a non-empty array");
  }
  if (v.length > MAX_TESTS) {
    throw new ValidationError(400, `at most ${MAX_TESTS} tests per assessment`);
  }
  return v.map((t, i) => {
    if (!isRecord(t)) throw new ValidationError(400, `test ${i}: must be an object`);
    if (typeof t.name !== "string" || t.name.length === 0 || t.name.length > MAX_TEST_NAME) {
      throw new ValidationError(400, `test ${i}: name must be 1-${MAX_TEST_NAME} characters`);
    }
    if (typeof t.expected_stdout !== "string") {
      throw new ValidationError(400, `test ${i}: expected_stdout must be a string`);
    }
    if (Buffer.byteLength(t.expected_stdout, "utf8") > MAX_OUTPUT_BYTES) {
      throw new ValidationError(413, `test ${i}: expected_stdout too large`);
    }
    return {
      name: t.name,
      stdin: validateStdin(t.stdin ?? ""),
      expectedStdout: t.expected_stdout,
      hidden: t.hidden === true,
    };
  });
}

/** Pick the entry file: exact entryFile match wins, otherwise the first file. */
export function pickEntry(files: FileInput[], entryFile: string): string {
  return files.find((f) => f.path === entryFile)?.path ?? files[0].path;
}

export function validateExecuteBody(body: unknown, maxTotalBytes: number): ValidatedExecute {
  if (!isRecord(body)) throw new ValidationError(400, "body must be a JSON object");
  return {
    runtimeId: validateRuntimeId(body.runtime),
    files: validateFiles(body.files, maxTotalBytes),
    stdin: validateStdin(body.stdin),
    timeoutSec: validateTimeout(body.timeout) ?? 0, // 0 = use runtime default
  };
}

export function validateAssessBody(body: unknown, maxTotalBytes: number): ValidatedAssess {
  const base = validateExecuteBody(body, maxTotalBytes);
  if (!isRecord(body)) throw new ValidationError(400, "body must be a JSON object");
  return { ...base, tests: validateTests(body.tests) };
}
