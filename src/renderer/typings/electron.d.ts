/**
 * Should match main/preload.ts for typescript support in renderer
 */
export default interface ElectronApi {
}

declare global {
  interface Window {
    electronAPI: ElectronApi
    primaryWindowAPI: {
      sendMessage: (message: string) => void
      showFramelessSampleWindow: () => void
      showTestVideoWindow: () => void
      openExternalLink: (url: string) => void
      clearAppConfiguration: () => void
      onShowExitAppMsgbox: (callback: () => void) => void
      onShowClosePrimaryWinMsgbox: (callback: () => void) => void
      asyncExitApp: () => Promise<void>
      minToTray: () => void
      httpGetRequest: (url: string) => void
    }
    testVideoAPI: {
      start: (config: { width: number; height: number; fps: number }) => Promise<{ success: boolean; config: any }>
      stop: () => void
      getStats: () => Promise<any>
      onFrameReady: (callback: (frameInfo: any) => void) => void
      readFrameData: (shmName?: string) => Promise<ArrayBuffer>
    }
  }
}
