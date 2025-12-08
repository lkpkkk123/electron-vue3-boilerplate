/**
 * Preload 脚本 - 暴露测试视频 API
 */
import { contextBridge, ipcRenderer } from "electron";

// 动态加载 shared memory addon
let sharedMemory: any = null;

try {
  const path = require("path");
  const addon_path = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addon_path);
} catch (err) {
  console.error("Failed to load shared memory addon:", err);
}

contextBridge.exposeInMainWorld("testVideoAPI", {
  // 启动测试视频生成
  start: (config: { width: number; height: number; fps: number }) => 
    ipcRenderer.invoke("test-image-start", config),
  
  // 停止
  stop: () => ipcRenderer.send("test-image-stop"),
  
  // 通知主进程帧已渲染完成
  notifyFrameRendered: () => ipcRenderer.send("test-image-frame-rendered"),
  
  // 获取统计信息
  getStats: () => ipcRenderer.invoke("test-image-stats"),
  
  // 监听帧就绪事件
  onFrameReady: (callback: (frameInfo: any) => void) => {
    ipcRenderer.on("test-frame-ready", (event, frameInfo) => {
      callback(frameInfo);
    });
  },
  
  // 读取帧数据
  readFrameData: async (shmName?: string): Promise<ArrayBuffer> => {
    if (shmName && sharedMemory) {
      try {
        // 从共享内存读取（有复制开销）
        const buffer = sharedMemory.read(shmName);
        return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
      } catch (err: any) {
        console.error("[Preload] Failed to read from shared memory:", err);
      }
    }
    
    // 回退到 IPC
    return ipcRenderer.invoke("test-image-get-frame-data");
  }
});
