/**
 * Preload 脚本 - 暴露测试视频 API
 */
import { ipcRenderer } from "electron";

// 动态加载 shared memory addon
let sharedMemory: any = null;

try {
  const path = require("path");
  const addon_path = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addon_path);
} catch (err) {
  console.error("Failed to load shared memory addon:", err);
}

(window as any).testVideoAPI = {
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

  // 填充数据到共享内存
  fillImageData: (buffer: Buffer, width: number, height: number): boolean => {
    if (!sharedMemory) {
      console.error("[Preload] Shared memory addon not loaded");
      return false;
    }
    try {
      // 填充测试图像（上1/3红色，中1/3绿色，下1/3蓝色）
      sharedMemory.fill(buffer, width, height);
      return true;
    } catch (err: any) {
      console.error("[Preload] Failed to fill shared memory:", err);
      return false;
    }
  },

  getImageData: (width: number, height: number): Buffer => {
    if (!sharedMemory) {
      console.error("[Preload] Shared memory addon not loaded");
      return Buffer.alloc(0);
    }
    try {
      // 填充测试图像（上1/3红色，中1/3绿色，下1/3蓝色）
      return sharedMemory.getImg(width, height);
    } catch (err: any) {
      console.error("[Preload] Failed to fill shared memory:", err);
      return Buffer.alloc(0);
    }
  },
  
  // 读取帧数据
  readFrameData: async (shmName?: string): Promise<number> => {
    if (shmName && sharedMemory) {
      try {
        // 从共享内存读取（有复制开销）
        const buffer = sharedMemory.read(shmName);
        (window as any).__LAST_SHARED_FRAME = buffer;
        //return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
        //console.log("retrun buffer.buffer");
        return 0;
      } catch (err: any) {
        console.error("[Preload] Failed to read from shared memory:", err);
      }
    }
    
    // 回退到 IPC
    return ipcRenderer.invoke("test-image-get-frame-data");
  }
};
