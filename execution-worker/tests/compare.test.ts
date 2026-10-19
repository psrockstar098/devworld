import { describe, expect, test } from "bun:test";
import { normalizeOutput, outputsEqual } from "../src/compare";

describe("normalizeOutput", () => {
  test("trims trailing newline differences", () => {
    expect(normalizeOutput("hello\n")).toBe("hello");
    expect(normalizeOutput("hello")).toBe("hello");
    expect(normalizeOutput("hello\n\n\n")).toBe("hello");
  });

  test("strips leading blank lines", () => {
    expect(normalizeOutput("\n\nhello")).toBe("hello");
  });

  test("normalizes CRLF and lone CR", () => {
    expect(normalizeOutput("a\r\nb\r\n")).toBe("a\nb");
    expect(normalizeOutput("a\rb\r")).toBe("a\nb");
  });

  test("strips trailing spaces and tabs per line", () => {
    expect(normalizeOutput("a   \nb\t\n")).toBe("a\nb");
  });

  test("keeps interior blank lines and indentation", () => {
    expect(normalizeOutput("a\n\n  b")).toBe("a\n\n  b");
  });

  test("empty and whitespace-only become empty", () => {
    expect(normalizeOutput("")).toBe("");
    expect(normalizeOutput("  \n \t\n")).toBe("");
  });
});

describe("outputsEqual", () => {
  test("equal after normalization", () => {
    expect(outputsEqual("42\n", "42")).toBe(true);
    expect(outputsEqual("1 2 3\r\n", "1 2 3")).toBe(true);
    expect(outputsEqual("line   \n", "line")).toBe(true);
  });

  test("detects real differences", () => {
    expect(outputsEqual("42", "43")).toBe(false);
    expect(outputsEqual("a\nb", "a\nc")).toBe(false);
    expect(outputsEqual("a\n\nb", "a\nb")).toBe(false); // interior blank line matters
    expect(outputsEqual("  indented", "indented")).toBe(false); // leading indent matters
  });
});
