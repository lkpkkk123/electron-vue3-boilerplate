import path from "path";
import { ipcMain } from "electron";
import WindowBase from "../window-base";

class TestVideoWindow extends WindowBase {
  constructor() {
    super({
      width: 1400,
      height: 900,
      webPreferences: {
        preload: path.join(__dirname, "../../../lib/test-video/main/test-video-preload.js"),
      },
      title: "共享内存测试视频播放器",
    });

    this.openRouter("/test-video");
  }

  protected registerIpcMainHandler(): void {
    // 这个窗口不需要额外的 IPC 处理器
    // 所有逻辑都在 test-video/main/index.ts 中
  }
}

export default TestVideoWindow;
