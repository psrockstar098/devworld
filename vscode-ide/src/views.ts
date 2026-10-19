// DEVWORLD views: test runner tree, commands, status bar. Registered through
// a synthetic local extension so views/commands appear in the real workbench.

import * as vscode from 'vscode'
import { registerExtension, ExtensionHostKind } from '@codingame/monaco-vscode-api/extensions'
import { vfs } from './vfs'
import { getTestCases, setTestCases, runAllTests, runActiveFile, formatReport, langForPath } from './gameide'
import { BACKEND_RUNTIME, getBackend, setBackend } from './backend'
import type { TestCase, TestResult } from './backend'

class TestItem extends vscode.TreeItem {
  constructor(
    public readonly tc: TestCase,
    public readonly result: TestResult | undefined,
    public readonly index: number
  ) {
    super(tc.name || `test ${index + 1}`, vscode.TreeItemCollapsibleState.None)
    this.description = tc.hidden ? 'hidden' : (tc.stdin ? 'stdin' : '')
    if (result) {
      this.iconPath = new vscode.ThemeIcon(result.pass ? 'pass' : 'error')
      this.tooltip = result.pass ? 'passed' : (result.hidden ? 'failed (hidden)' : formatReport([result]))
    } else {
      this.iconPath = new vscode.ThemeIcon('circle-outline')
    }
    this.contextValue = 'devworldTest'
  }
}

class TestsProvider implements vscode.TreeDataProvider<TestItem> {
  private emitter = new vscode.EventEmitter<void>()
  readonly onDidChangeTreeData = this.emitter.event
  private lastResults: TestResult[] = []

  refresh(results?: TestResult[]): void {
    if (results) this.lastResults = results
    this.emitter.fire()
  }

  getTreeItem(el: TestItem): vscode.TreeItem { return el }

  getChildren(): TestItem[] {
    const uri = vscode.window.activeTextEditor?.document.uri
    if (!uri) return []
    const path = vfs.pathFromUri(uri)
    const cases = getTestCases(path)
    return cases.map((tc, i) => {
      const r = this.lastResults.find((x) => x.name === tc.name)
      return new TestItem(tc, r, i)
    })
  }
}

export const testsProvider = new TestsProvider()

export async function registerDevworldExtension(): Promise<typeof vscode> {
  const { getApi } = registerExtension(
    {
      name: 'devworld',
      publisher: 'devworld',
      version: '0.3.0',
      engines: { vscode: '*' },
      contributes: {
        views: {
          explorer: [
            { id: 'devworldTests', name: 'DEVWORLD Tests' },
            { id: 'devworldGit', name: 'DEVWORLD Git' }
          ]
        },
        commands: [
          { command: 'devworld.runFile', title: 'DEVWORLD: Run Active File' },
          { command: 'devworld.runTests', title: 'DEVWORLD: Run Tests' },
          { command: 'devworld.setBackend', title: 'DEVWORLD: Set Sandbox Backend' },
          { command: 'devworld.testAdd', title: 'DEVWORLD: Add Test Case', icon: '$(add)' },
          { command: 'devworld.testDelete', title: 'DEVWORLD: Delete Test Case', icon: '$(trash)' }
        ],
        menus: {
          'view/title': [
            { command: 'devworld.testAdd', when: 'view == devworldTests', group: 'navigation' },
            { command: 'devworld.runTests', when: 'view == devworldTests', group: 'navigation' }
          ],
          'view/item/context': [
            { command: 'devworld.testDelete', when: 'view == devworldTests && viewItem == devworldTest' }
          ]
        }
      }
    } as any,
    ExtensionHostKind.LocalProcess
  )
  const api = await getApi()

  api.window.registerTreeDataProvider('devworldTests', testsProvider)

  api.commands.registerCommand('devworld.runFile', () => runActiveFile())
  api.commands.registerCommand('devworld.runTests', async () => {
    const uri = api.window.activeTextEditor?.document.uri
    const summary = await runAllTests()
    if (uri) {
      const path = vfs.pathFromUri(uri)
      // re-read last results via a fresh run is wasteful; refresh from stored state
      testsProvider.refresh()
    }
    void summary
  })
  api.commands.registerCommand('devworld.setBackend', async () => {
    const cur = getBackend()
    const url = await api.window.showInputBox({
      prompt: 'Sandbox backend URL (execution-worker)',
      value: cur?.url || '',
      placeHolder: 'https://your-worker.example.com'
    })
    if (url === undefined) return
    const key = await api.window.showInputBox({
      prompt: 'API key (x-api-key)',
      password: true,
      placeHolder: 'leave empty if none'
    })
    if (key === undefined) return
    setBackend({ url: url.trim(), apiKey: key })
    updateBackendStatus()
    api.window.showInformationMessage(url.trim() ? 'Sandbox backend configured.' : 'Sandbox backend cleared.')
  })
  api.commands.registerCommand('devworld.testAdd', async () => {
    const uri = api.window.activeTextEditor?.document.uri
    if (!uri) {
      api.window.showInformationMessage('Open a file first.')
      return
    }
    const path = vfs.pathFromUri(uri)
    const name = await api.window.showInputBox({ prompt: 'Test name', value: `test ${getTestCases(path).length + 1}` })
    if (!name) return
    const stdin = await api.window.showInputBox({ prompt: 'stdin (optional)' })
    if (stdin === undefined) return
    const expected = await api.window.showInputBox({ prompt: 'Expected stdout' })
    if (expected === undefined) return
    const cases = getTestCases(path)
    cases.push({ name, stdin: stdin || '', expected: expected || '' })
    setTestCases(path, cases)
    testsProvider.refresh()
  })
  api.commands.registerCommand('devworld.testDelete', async (item: TestItem) => {
    const uri = api.window.activeTextEditor?.document.uri
    if (!uri || !item) return
    const path = vfs.pathFromUri(uri)
    const cases = getTestCases(path)
    cases.splice(item.index, 1)
    setTestCases(path, cases)
    testsProvider.refresh()
  })

  // refresh test view when the active editor changes
  api.window.onDidChangeActiveTextEditor(() => testsProvider.refresh())

  return api
}

let backendStatus: vscode.StatusBarItem | null = null

export function updateBackendStatus(): void {
  if (!backendStatus) {
    backendStatus = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100)
    backendStatus.command = 'devworld.setBackend'
  }
  const b = getBackend()
  const uri = vscode.window.activeTextEditor?.document.uri
  const lang = uri ? langForPath(vfs.pathFromUri(uri)) : null
  if (lang && lang.run === 'server' && BACKEND_RUNTIME[lang.id]) {
    backendStatus.text = b ? '$(check) Sandbox ready' : '$(alert) Sandbox: not configured'
    backendStatus.tooltip = b ? `Backend: ${b.url}` : 'Click to configure the sandbox backend'
    backendStatus.show()
  } else {
    backendStatus.hide()
  }
}

export function initStatusBar(): void {
  vscode.window.onDidChangeActiveTextEditor(() => updateBackendStatus())
  updateBackendStatus()
}
