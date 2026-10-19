/* Unit tests for the IDE dependency/submission store.
   Run: node --test game-ide/tests/   (Node 18+; no dependencies) */
const test = require("node:test");
const assert = require("node:assert");
const path = require("node:path");

const DWDeps = require(path.join(__dirname, "..", "classic", "deps-store.js"));

function fakeStore(initial = {}) {
  const data = { ...initial };
  return {
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => {
      data[k] = String(v);
    },
    _data: data,
  };
}

test("get returns a copy and defaults to an empty list", () => {
  DWDeps._setStore(fakeStore());
  assert.deepEqual(DWDeps.get("python"), []);
  DWDeps.set("python", ["numpy"]);
  const a = DWDeps.get("python");
  a.push("tampered");
  assert.deepEqual(DWDeps.get("python"), ["numpy"], "returned array must not alias storage");
});

test("set deduplicates and is per-runtime", () => {
  DWDeps._setStore(fakeStore());
  DWDeps.set("python", ["numpy", "numpy", "pandas"]);
  DWDeps.set("javascript", ["lodash"]);
  assert.deepEqual(DWDeps.get("python"), ["numpy", "pandas"]);
  assert.deepEqual(DWDeps.get("javascript"), ["lodash"]);
  assert.deepEqual(DWDeps.get("go"), []);
});

test("set replaces the previous selection for a runtime", () => {
  DWDeps._setStore(fakeStore());
  DWDeps.set("python", ["numpy"]);
  DWDeps.set("python", []);
  assert.deepEqual(DWDeps.get("python"), []);
});

test("survives corrupt stored JSON", () => {
  DWDeps._setStore(fakeStore({ "devworld.deps.v1": "{ not json" }));
  assert.deepEqual(DWDeps.get("python"), []);
});

test("tracks the submission id", () => {
  DWDeps._setStore(fakeStore());
  assert.equal(DWDeps.submission(), "");
  DWDeps.setSubmission("3f0c-abc");
  assert.equal(DWDeps.submission(), "3f0c-abc");
  DWDeps.setSubmission("");
  assert.equal(DWDeps.submission(), "");
});

test("works with no store at all (SSR / import)", () => {
  DWDeps._setStore(null);
  assert.deepEqual(DWDeps.get("python"), []);
  assert.equal(DWDeps.submission(), "");
  DWDeps.set("python", ["numpy"]); // must not throw
  assert.deepEqual(DWDeps.get("python"), []);
});
