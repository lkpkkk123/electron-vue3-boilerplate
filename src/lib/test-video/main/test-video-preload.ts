/**
 * Preload 脚本 - 暴露测试视频 API
 */
import { contextBridge, ipcRenderer } from "electron";

// 动态加载 shared memory addon
let sharedMemory: any = null;
try {
  const path = require("path");
  // 从 build/lib/test-video/main/ 访问 native/shared-memory/build/Release/
  const addon_path = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addon_path);
  console.log("✅ Shared memory addon loaded successfully");
} catch (err) {
  console.error("❌ Failed to load shared memory addon:", err);
  console.log("Will fall back to IPC data transfer");
}

contextBridge.exposeInMainWorld("testVideoAPI", {
  // 启动测试视频生成
  start: (config: { width: number; height: number; fps: number }) => 
    ipcRenderer.invoke("test-image-start", config),
  
  // 停止
  stop: () => ipcRenderer.send("test-image-stop"),
  
  // 获取统计信息
  getStats: () => ipcRenderer.invoke("test-image-stats"),
  
  // 监听帧就绪事件
  onFrameReady: (callback: (frameInfo: any) => void) => {
    ipcRenderer.on("test-frame-ready", (event, frameInfo) => {
      callback(frameInfo);
    });
  },
  
  // 读取帧数据 - 优先使用共享内存，否则使用 IPC
  readFrameData: async (shmName?: string): Promise<ArrayBuffer> => {
    if (shmName && sharedMemory) {
      try {
        const buffer = sharedMemory.read(shmName);
        return buffer.buffer;
      } catch (err) {
        console.error("Failed to read from shared memory, falling back to IPC:", err);
      }
    }
    // 回退到 IPC
    return ipcRenderer.invoke("test-image-get-frame-data");
  }
});
