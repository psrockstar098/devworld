// Vite plugin: fixes the TextMate worker tokenizer's dynamic import of
// '@codingame/monaco-vscode-api/_virtual/main'. Vite's bundler mangles the
// `.then(n => n.main)` chain when inlining the dynamic import, leaving it
// resolving to undefined and throwing on every background tokenization pass.
// We rewrite the source to import our ESM wrapper (which has static named
// exports) and drop the `.main` accessor.
export function textmateVirtualMainFix() {
  return {
    name: 'textmate-virtual-main-fix',
    enforce: 'pre' as const,
    transform(code: string, id: string) {
      if (!id.includes('textMateWorkerTokenizerController')) return null
      const from =
        "await import('@codingame/monaco-vscode-api/_virtual/main').then(function (n) { return n.main; })"
      if (!code.includes(from)) return null
      const to = "await import('textmate-main-shim')"
      return { code: code.replace(from, to), map: null }
    }
  }
}
