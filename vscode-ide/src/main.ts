// DEVWORLD IDE — real VS Code in the browser (monaco-vscode-api v37).
// Full workbench: Explorer, search, terminal, problems, settings, command
// palette, real extensions + language servers, backed by our localStorage VFS.

import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker'
import ExtensionHostWorker from '@codingame/monaco-vscode-api/workers/extensionHost.worker?worker'
import TextMateWorker from '@codingame/monaco-vscode-textmate-service-override/worker?worker'
import OutputLinkWorker from '@codingame/monaco-vscode-output-service-override/worker?worker'
import LanguageDetectionWorker from '@codingame/monaco-vscode-language-detection-worker-service-override/worker?worker'
import NotebookWorker from '@codingame/monaco-vscode-notebook-service-override/worker?worker'
import SearchWorker from '@codingame/monaco-vscode-search-service-override/worker?worker'

import {
  initialize as initializeMonacoService,
  type IEditorOverrideServices,
  type IWorkbenchConstructionOptions,
  LogLevel
} from '@codingame/monaco-vscode-api'
import { initUserConfiguration } from '@codingame/monaco-vscode-configuration-service-override/common'
import { createIndexedDBProviders } from '@codingame/monaco-vscode-files-service-override'
import { registerFileSystemOverlay } from '@codingame/monaco-vscode-files-service-override'
import getWorkbenchServiceOverride from '@codingame/monaco-vscode-workbench-service-override'
import getQuickAccessServiceOverride from '@codingame/monaco-vscode-quickaccess-service-override'

// --- service overrides (mirrors the upstream demo's known-good classic set) ---
import getAccessibilityServiceOverride from '@codingame/monaco-vscode-accessibility-service-override'
import getAiServiceOverride from '@codingame/monaco-vscode-ai-service-override'
import getAssignmentServiceOverride from '@codingame/monaco-vscode-assignment-service-override'
import getAuthenticationServiceOverride from '@codingame/monaco-vscode-authentication-service-override'
import getBannerServiceOverride from '@codingame/monaco-vscode-view-banner-service-override'
import getChatServiceOverride from '@codingame/monaco-vscode-chat-service-override'
import getCommentsServiceOverride from '@codingame/monaco-vscode-comments-service-override'
import getConfigurationServiceOverride from '@codingame/monaco-vscode-configuration-service-override'
import getDebugServiceOverride from '@codingame/monaco-vscode-debug-service-override'
import getDialogsServiceOverride from '@codingame/monaco-vscode-dialogs-service-override'
import getEditSessionsServiceOverride from '@codingame/monaco-vscode-edit-sessions-service-override'
import getEmmetServiceOverride from '@codingame/monaco-vscode-emmet-service-override'
import getEnvironmentServiceOverride from '@codingame/monaco-vscode-environment-service-override'
import getExplorerServiceOverride from '@codingame/monaco-vscode-explorer-service-override'
import getExtensionGalleryServiceOverride from '@codingame/monaco-vscode-extension-gallery-service-override'
import getExtensionServiceOverride from '@codingame/monaco-vscode-extensions-service-override'
import getGithubServiceOverride from '@codingame/monaco-vscode-github-service-override'
import getImageResizeServiceOverride from '@codingame/monaco-vscode-image-resize-service-override'
import getInteractiveServiceOverride from '@codingame/monaco-vscode-interactive-service-override'
import getIssueServiceOverride from '@codingame/monaco-vscode-issue-service-override'
import getKeybindingsServiceOverride from '@codingame/monaco-vscode-keybindings-service-override'
import getLanguageDetectionWorkerServiceOverride from '@codingame/monaco-vscode-language-detection-worker-service-override'
import getLanguagesServiceOverride from '@codingame/monaco-vscode-languages-service-override'
import getLifecycleServiceOverride from '@codingame/monaco-vscode-lifecycle-service-override'
import getLocalizationServiceOverride from '@codingame/monaco-vscode-localization-service-override'
import getLogServiceOverride from '@codingame/monaco-vscode-log-service-override'
import getMarkersServiceOverride from '@codingame/monaco-vscode-markers-service-override'
import getMcpServiceOverride from '@codingame/monaco-vscode-mcp-service-override'
import getMeteredConnectionServiceOverride from '@codingame/monaco-vscode-metered-connection-service-override'
import getModelServiceOverride from '@codingame/monaco-vscode-model-service-override'
import getMultiDiffEditorServiceOverride from '@codingame/monaco-vscode-multi-diff-editor-service-override'
import getNotebookServiceOverride from '@codingame/monaco-vscode-notebook-service-override'
import getNotificationServiceOverride from '@codingame/monaco-vscode-notifications-service-override'
import getOutlineServiceOverride from '@codingame/monaco-vscode-outline-service-override'
import getOutputServiceOverride from '@codingame/monaco-vscode-output-service-override'
import getPerformanceServiceOverride from '@codingame/monaco-vscode-performance-service-override'
import getPreferencesServiceOverride from '@codingame/monaco-vscode-preferences-service-override'
import getProcessControllerServiceOverride from '@codingame/monaco-vscode-process-explorer-service-override'
import getRelauncherServiceOverride from '@codingame/monaco-vscode-relauncher-service-override'
import getRemoteAgentServiceOverride from '@codingame/monaco-vscode-remote-agent-service-override'
import getScmServiceOverride from '@codingame/monaco-vscode-scm-service-override'
import getSearchServiceOverride from '@codingame/monaco-vscode-search-service-override'
import getSecretStorageServiceOverride from '@codingame/monaco-vscode-secret-storage-service-override'
import getShareServiceOverride from '@codingame/monaco-vscode-share-service-override'
import getSnippetServiceOverride from '@codingame/monaco-vscode-snippets-service-override'
import getSpeechServiceOverride from '@codingame/monaco-vscode-speech-service-override'
import getStatusBarServiceOverride from '@codingame/monaco-vscode-view-status-bar-service-override'
import getStorageServiceOverride from '@codingame/monaco-vscode-storage-service-override'
import getSurveyServiceOverride from '@codingame/monaco-vscode-survey-service-override'
import getTaskServiceOverride from '@codingame/monaco-vscode-task-service-override'
import getTelemetryServiceOverride from '@codingame/monaco-vscode-telemetry-service-override'
import getTerminalServiceOverride from '@codingame/monaco-vscode-terminal-service-override'
import getTestingServiceOverride from '@codingame/monaco-vscode-testing-service-override'
import getTextmateServiceOverride from '@codingame/monaco-vscode-textmate-service-override'
import getThemeServiceOverride from '@codingame/monaco-vscode-theme-service-override'
import getTimelineServiceOverride from '@codingame/monaco-vscode-timeline-service-override'
import getTitleBarServiceOverride from '@codingame/monaco-vscode-view-title-bar-service-override'
import getTreeSitterServiceOverride from '@codingame/monaco-vscode-treesitter-service-override'
import getUpdateServiceOverride from '@codingame/monaco-vscode-update-service-override'
import getUserDataProfileServiceOverride from '@codingame/monaco-vscode-user-data-profile-service-override'
import getUserDataSyncServiceOverride from '@codingame/monaco-vscode-user-data-sync-service-override'
import getWalkThroughServiceOverride from '@codingame/monaco-vscode-walkthrough-service-override'
import getWelcomeServiceOverride from '@codingame/monaco-vscode-welcome-service-override'
import getWorkingCopyServiceOverride from '@codingame/monaco-vscode-working-copy-service-override'
import getWorkspaceTrustOverride from '@codingame/monaco-vscode-workspace-trust-service-override'

// --- default extensions: theme, grammars for every language, real language servers ---
import '@codingame/monaco-vscode-theme-defaults-default-extension'
import '@codingame/monaco-vscode-javascript-default-extension'
import '@codingame/monaco-vscode-typescript-basics-default-extension'
import '@codingame/monaco-vscode-typescript-language-features-default-extension'
import '@codingame/monaco-vscode-json-language-features-default-extension'
import '@codingame/monaco-vscode-html-language-features-default-extension'
import '@codingame/monaco-vscode-css-language-features-default-extension'
import '@codingame/monaco-vscode-python-default-extension'

// NOTE: ms-pylance.vsix / ms-python.vsix REMOVED 2026-10-19 — Pylance is a
// native Node binary and cannot run in a browser extension host; the vsix
// imports were dead weight that poisoned the extension host (dirname crash).
// Python language-server support in-browser is honestly unavailable; Python
// keeps syntax highlighting + the Pyodide runner.
// NOTE: all-language-default-extensions omitted — its 100+ TextMate grammars
// blow the build memory budget; the key languages above are covered.

import 'vscode/localExtensionHost'
import * as vscode from 'vscode'

import { vfs, DevworldFileSystemProvider } from './vfs'
import { installGameIDE } from './gameide'
import { registerDevworldExtension, initStatusBar } from './views'
import { registerGitView } from './git'
import { getTerminalBackend } from './terminal'

/* ---------------- workers ---------------- */
// The extension host bootstraps through an iframe and needs a real URL for
// 'extensionHostWorkerMain'. scripts/copy-worker.js (run after vite build)
// copies the hashed worker asset to ./workers/extensionHost.worker.js.
const EXTENSION_HOST_WORKER_URL = './workers/extensionHost.worker.js'

self.MonacoEnvironment = {
  getWorker(_moduleId: string, label: string) {
    switch (label) {
      case 'extensionHostWorkerMain': return new ExtensionHostWorker()
      case 'TextMateWorker': return new TextMateWorker()
      case 'OutputLinkDetectionWorker': return new OutputLinkWorker()
      case 'LanguageDetectionWorker': return new LanguageDetectionWorker()
      case 'NotebookEditorWorker': return new NotebookWorker()
      case 'LocalFileSearchWorker': return new SearchWorker()
      default: return new EditorWorker()
    }
  },
  getWorkerUrl(_moduleId: string, label: string): string | undefined {
    if (label === 'extensionHostWorkerMain') {
      return new URL(EXTENSION_HOST_WORKER_URL, document.baseURI).toString()
    }
    return undefined
  },
  getWorkerOptions(_moduleId: string, _label: string): WorkerOptions {
    return { type: 'module' }
  }
}

/* ---------------- local services ---------------- */
export const userDataProvider = await createIndexedDBProviders()

await initUserConfiguration(JSON.stringify({
  'workbench.colorTheme': 'Default Dark+',
  'workbench.startupEditor': 'none',
  'editor.fontSize': 14,
  'editor.minimap.enabled': false
}))

/* ---------------- virtual filesystem ---------------- */
vfs.seed()
const fsProvider = new DevworldFileSystemProvider()
// Bridge: the overlay API wants the internal provider interface; our provider
// implements the same shape (stat/readDirectory/readFile/writeFile/...).
// It reads live from the localStorage-backed VFS, so Explorer/search/tabs
// always see current files.
registerFileSystemOverlay(1, fsProvider as unknown as Parameters<typeof registerFileSystemOverlay>[1])

/* ---------------- service overrides ---------------- */
const alternateDomainPatternUrl = new URL('.', import.meta.url)
alternateDomainPatternUrl.pathname = ''
if (alternateDomainPatternUrl.hostname.includes('localhost')) {
  alternateDomainPatternUrl.hostname = `{{uuid}}.${alternateDomainPatternUrl.hostname}`
}
const alternateDomainPattern = alternateDomainPatternUrl.href

const services: IEditorOverrideServices = {
  ...getAuthenticationServiceOverride(),
  ...getLogServiceOverride(),
  ...getExtensionServiceOverride({ enableWorkerExtensionHost: true }),
  ...getModelServiceOverride(),
  ...getNotificationServiceOverride(),
  ...getKeybindingsServiceOverride(),
  ...getTextmateServiceOverride(),
  ...getTreeSitterServiceOverride(),
  ...getThemeServiceOverride(),
  ...getLanguagesServiceOverride(),
  ...getPreferencesServiceOverride(),
  ...getBannerServiceOverride(),
  ...getStatusBarServiceOverride(),
  ...getOutputServiceOverride(),
  ...getTerminalServiceOverride(getTerminalBackend()),
  ...getAccessibilityServiceOverride(),
  ...getLanguageDetectionWorkerServiceOverride(),
  ...getStorageServiceOverride({
    forcedValues: {
      'chat.setupContext': {
        entitlement: 'Enterprise' as never,
        organisations: undefined,
        sku: undefined,
        copilotTrackingId: undefined,
        registered: true,
        completed: true,
        installed: true
      }
    },
    fallbackOverride: { 'workbench.activity.showAccounts': false }
  }),
  ...getRemoteAgentServiceOverride({ scanRemoteExtensions: true }),
  ...getLifecycleServiceOverride(),
  ...getEnvironmentServiceOverride(),
  ...getWorkspaceTrustOverride(),
  ...getWorkingCopyServiceOverride(),
  ...getScmServiceOverride(),
  ...getNotebookServiceOverride(),
  ...getUserDataProfileServiceOverride(),
  ...getAiServiceOverride(),
  ...getTaskServiceOverride(),
  ...getCommentsServiceOverride(),
  ...getEditSessionsServiceOverride(),
  ...getEmmetServiceOverride(),
  ...getInteractiveServiceOverride(),
  ...getIssueServiceOverride(),
  ...getMultiDiffEditorServiceOverride(),
  ...getPerformanceServiceOverride(),
  ...getRelauncherServiceOverride(),
  ...getShareServiceOverride(),
  ...getSpeechServiceOverride(),
  ...getSurveyServiceOverride(),
  ...getUpdateServiceOverride(),
  ...getExplorerServiceOverride(),
  ...getLocalizationServiceOverride({
    async clearLocale() {
      const url = new URL(window.location.href)
      url.searchParams.delete('locale')
      window.history.pushState(null, '', url.toString())
    },
    async setLocale(id) {
      const url = new URL(window.location.href)
      url.searchParams.set('locale', id)
      window.history.pushState(null, '', url.toString())
    },
    availableLanguages: [{ locale: 'en', languageName: 'English' }]
  }),
  ...getProcessControllerServiceOverride(),
  ...getSecretStorageServiceOverride(),
  ...getDialogsServiceOverride(),
  ...getConfigurationServiceOverride(),
  ...getMarkersServiceOverride(),
  ...getGithubServiceOverride(),
  ...getTitleBarServiceOverride(),
  // classic workbench extras
  ...getOutlineServiceOverride(),
  ...getSnippetServiceOverride(),
  ...getSearchServiceOverride(),
  ...getChatServiceOverride({
    defaultAccount: {
      entitlementsData: {
        access_type_sku: 'unused', assigned_date: 'unused', can_signup_for_limited: false,
        copilot_plan: 'enterprise', organization_login_list: [], analytics_tracking_id: 'unused', chat_enabled: false
      },
      accountName: 'unused',
      authenticationProvider: { id: 'unused', name: 'unused', enterprise: true },
      enterprise: true, sessionId: 'unused'
    }
  }),
  ...getWelcomeServiceOverride(),
  ...getWalkThroughServiceOverride(),
  ...getUserDataSyncServiceOverride(),
  ...getTelemetryServiceOverride(),
  ...getMcpServiceOverride(),
  ...getMeteredConnectionServiceOverride(),
  ...getTimelineServiceOverride(),
  ...getTestingServiceOverride(),
  ...getDebugServiceOverride(),
  ...getExtensionGalleryServiceOverride({
    webOnly: true,
    // Point the Extensions view at Open VSX (the open marketplace) instead of
    // Microsoft's gallery, which blocks non-VS-Code clients. This is what lets
    // a dev install their own language extension, just like the real world.
    transformExtensionGalleryManifest: (manifest) => ({
      // If the manifest request fails, hand back a valid empty manifest instead
      // of one missing the required `version`/`resources`/`capabilities`.
      ...(manifest ?? { version: '1.0.0', resources: [], capabilities: { extensionQuery: {} } }),
      serviceUrl: 'https://open-vsx.org/api'
    })
  }),
  ...getWorkbenchServiceOverride(undefined, alternateDomainPattern),
  ...getQuickAccessServiceOverride({
    isKeybindingConfigurationVisible: () => true,
    shouldUseGlobalPicker: () => true
  })
}

/* ---------------- workbench ---------------- */
const container = document.getElementById('app')!

const constructOptions: IWorkbenchConstructionOptions = {
  // Workspace trust disabled: our VFS is local-only, so everything is trusted.
  // (The TS language extension requires a trusted workspace to activate.)
  enableWorkspaceTrust: false,
  windowIndicator: { label: 'DEVWORLD', tooltip: '', command: '' },
  developmentOptions: { logLevel: LogLevel.Info },
  configurationDefaults: { 'window.title': 'DEVWORLD — Game IDE' },
  workspaceProvider: {
    trusted: true,
    async open() {
      window.open(window.location.href)
      return true
    },
    workspace: { folderUri: vscode.Uri.file('/workspace') }
  },
  defaultLayout: {
    editors: [{ uri: vscode.Uri.file('/workspace/README.md'), viewColumn: 1 }]
  },
  productConfiguration: {
    nameShort: 'DEVWORLD',
    nameLong: 'DEVWORLD — Game IDE',
    // Extension marketplace: Open VSX (Microsoft's gallery blocks third-party clients)
    // monaco-vscode-api's vendored product type declares the gallery config
    // fields (controlUrl, extensionUrlTemplate, …) as required, but VS Code's
    // product schema has them optional and the web-only gallery code consults
    // `serviceUrl` alone — so only that is set. Extra endpoints would be
    // invented URLs that the client does not need.
    extensionsGallery: {
      serviceUrl: 'https://open-vsx.org/api'
    } as NonNullable<IWorkbenchConstructionOptions['productConfiguration']>['extensionsGallery']
  }
}

await initializeMonacoService(services, container, constructOptions, {
  userHome: vscode.Uri.file('/')
})

/* ---------------- DEVWORLD wiring ---------------- */
const api = await registerDevworldExtension()
registerGitView(api)
installGameIDE()
initStatusBar()

// remove the boot splash once the workbench has rendered
requestAnimationFrame(() => {
  setTimeout(() => document.getElementById('boot')?.remove(), 500)
})
