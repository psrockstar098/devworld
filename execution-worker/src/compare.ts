/**
 * Output comparison for mission assessment.
 *
 * Student output and expected output are compared after normalization so
 * that trivial whitespace differences do not fail a test:
 *  - CRLF / CR line endings become LF
 *  - trailing spaces/tabs are stripped from every line
 *  - leading and trailing blank lines are removed
 * Interior blank lines and indentation are significant.
 */

export function normalizeOutput(s: string): string {
  return s
    .replace(/\r\n?/g, "\n")
    .split("\n")
    .map((line) => line.replace(/[ \t]+$/, ""))
    .join("\n")
    .replace(/^\n+/, "")
    .replace(/\n+$/, "");
}

export function outputsEqual(actual: string, expected: string): boolean {
  return normalizeOutput(actual) === normalizeOutput(expected);
}
