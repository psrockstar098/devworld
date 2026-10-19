// ESM wrapper for vscode-textmate's _virtual/main.
// The prebuilt _virtual/main.js uses CJS interop that vite's dynamic-import
// transform mishandles (its `.main` ends up undefined at runtime, breaking
// TextMate background tokenization). This wrapper exposes the same shape
// with statically analyzable named exports.
import * as virtualMain from '../../node_modules/@codingame/monaco-vscode-api/_virtual/main.js'

const tm: any = (virtualMain as any).main ?? virtualMain

export const main = tm
export const applyStateStackDiff = tm.applyStateStackDiff
export const INITIAL = tm.INITIAL
export default tm
