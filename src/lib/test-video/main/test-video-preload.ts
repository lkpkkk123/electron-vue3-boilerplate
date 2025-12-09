/**
 * Preload 脚本 - 暴露测试视频 API
 */
import { ipcRenderer } from "electron";

// 动态加载 shared memory addon
let sharedMemory: any = null;
let vaapiDecoder: any = null;
let currentDecoder: any = null;

try {
  const path = require("path");
  const addon_path = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addon_path);
} catch (err) {
  console.error("Failed to load shared memory addon:", err);
}

// 加载 VA-API 解码器
try {
  const path = require("path");
  const decoder_path = path.join(__dirname, "../../../../native/vaapi-decoder/build/Release/vaapi_decoder.node");
  vaapiDecoder = require(decoder_path);
  console.log("VA-API decoder loaded successfully");
} catch (err) {
  console.error("Failed to load VA-API decoder addon:", err);
}

(window as any).testVideoAPI = {
  // 启动测试视频生成
  start: (config: { width: number; height: number; fps: number }) => 
    ipcRenderer.invoke("test-image-start", config),
  
  // 停止
  stop: () => {
    // 关闭解码器
    if (currentDecoder) {
      try {
        currentDecoder.close();
        console.log("[Preload] Video decoder closed");
      } catch (err) {
        console.error("[Preload] Error closing decoder:", err);
      }
      currentDecoder = null;
    }
    ipcRenderer.send("test-image-stop");
  },
  
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

  getImageFromVideo: (width: number, height: number): Buffer => {
    if (!vaapiDecoder) {
      console.error("[Preload] VA-API decoder addon not loaded");
      return Buffer.alloc(0);
    }

    try {
      // 如果解码器未初始化，则初始化
      if (!currentDecoder) {
        console.log("[Preload] Initializing VA-API decoder...");
        currentDecoder = new vaapiDecoder.VaapiDecoder();
        const videoPath = "/home/likp/Public/osd2.265";
        
        console.log("[Preload] Opening video file:", videoPath);
        
        // 检查文件是否存在
        const fs = require('fs');
        if (!fs.existsSync(videoPath)) {
          console.error("[Preload] Video file does not exist:", videoPath);
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        console.log("[Preload] File exists, size:", fs.statSync(videoPath).size, "bytes");
        
        const success = currentDecoder.initFromFile(videoPath);
        if (!success) {
          const error = currentDecoder.getLastError ? currentDecoder.getLastError() : "Unknown error";
          console.error("[Preload] Failed to initialize decoder with:", videoPath);
          console.error("[Preload] Error:", error);
          console.error("[Preload] Possible causes:");
          console.error("  1. VA-API device not accessible (check /dev/dri/renderD128)");
          console.error("  2. Unsupported video codec");
          console.error("  3. Corrupted video file");
          
          // 检查 VA-API 设备
          try {
            const drmPath = "/dev/dri/renderD128";
            if (fs.existsSync(drmPath)) {
              const stats = fs.statSync(drmPath);
              console.log("[Preload] DRM device exists, mode:", stats.mode.toString(8));
            } else {
              console.error("[Preload] DRM device not found:", drmPath);
            }
          } catch (e) {
            console.error("[Preload] Error checking DRM device:", e);
          }
          
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        
        // 获取视频信息
        const info = currentDecoder.getVideoInfo();
        if (info) {
          console.log(`[Preload] Video initialized: ${info.width}x${info.height} @ ${info.fps} fps, codec: ${info.codec}`);
        } else {
          console.error("[Preload] Failed to get video info");
        }
      }

      // 解码一帧
      const frame = currentDecoder.decodeFrame();
      if (frame) {
        // 返回 NV12 格式的帧数据
        if (Math.random() < 0.01) { // 每 100 帧输出一次以减少日志
          console.log(`[Preload] Decoded frame: ${frame.width}x${frame.height}, ${(frame.data.length / 1024 / 1024).toFixed(2)} MB`);
        }
        return frame.data;
      } else {
        // 到达文件末尾，重新初始化解码器以循环播放
        console.log("[Preload] End of video, restarting...");
        currentDecoder.close();
        currentDecoder = null;
        
        // 递归调用重新开始
        return (window as any).testVideoAPI.getImageFromVideo(width, height);
      }
    } catch (err: any) {
      console.error("[Preload] Failed to decode frame:", err);
      console.error("[Preload] Error stack:", err.stack);
      if (currentDecoder) {
        try {
          currentDecoder.close();
        } catch (e) {
          console.error("[Preload] Error closing decoder:", e);
        }
        currentDecoder = null;
      }
      return Buffer.alloc(0);
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
