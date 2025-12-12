/**
 * Preload 脚本 - 暴露测试视频 API
 */
import { ipcRenderer } from "electron";
import fs from "fs";
// 动态加载 shared memory addon
let sharedMemory: any = null;
let pureVaapiDecoder: any = null;
let currentDecoder: any = null;

try {
  const path = require("path");
  const addon_path = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addon_path);
} catch (err) {
  console.error("Failed to load shared memory addon:", err);
}

// 加载纯 VA-API 解码器（不依赖 FFmpeg）
try {
  const path = require("path");
  const decoder_path = path.join(__dirname, "../../../../native/vaapi-decoder/build/Release/pure_vaapi_decoder.node");
  pureVaapiDecoder = require(decoder_path);
  console.log("Pure VA-API decoder loaded successfully");
} catch (err) {
  console.error("Failed to load pure VA-API decoder addon:", err);
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
        //currentDecoder.close();
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
    if (!pureVaapiDecoder) {
      console.error("[Preload] Pure VA-API decoder addon not loaded");
      return Buffer.alloc(0);
    }

    try {
      // 如果解码器未初始化，则初始化
      if (!currentDecoder) {
        console.log("[Preload] Initializing pure VA-API decoder...");
        currentDecoder = new pureVaapiDecoder.PureVaapiDecoder();
        //const videoPath = "/home/zs/118.mp4";
        const videoPath = "/home/likp/Public/osd2.mp4";
        
        console.log("[Preload] Opening H.265 raw stream:", videoPath);
        
        // 检查文件是否存在
        if (!fs.existsSync(videoPath)) {
          console.error("[Preload] Video file does not exist:", videoPath);
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        console.log("[Preload] File exists, size:", fs.statSync(videoPath).size, "bytes");
        
        const success = currentDecoder.init(videoPath);
        console.log("[Preload] init finsh success =", success);

        if (!success) {
          const error =  "Unknown error";
          console.error("[Preload] Failed to initialize decoder");
          console.error("[Preload] Error:", error);
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        
        // 获取视频信息
        console.log("currentDecoder.getVideoInfo() in");
        const info = currentDecoder.getVideoInfo();
        console.log("currentDecoder.getVideoInfo() out");

        if (info) {
          console.log(`[Preload] Video initialized: ${info.width}x${info.height}`);
        }
      }

      // 解码一帧
      //console.log("[Preload] Decoding frame...");
      const frame = currentDecoder.decodeFrame();
      //console.log("[Preload] Decoding frame out...");

      if (frame) {
        // 返回 NV12 格式的帧数据
        return frame.data;
      } else {
        // 到达文件末尾，重置解码器循环播放
        console.log("[Preload] End of video, resetting...");
        //currentDecoder.reset();
        
        // 再次尝试解码
        const firstFrame = currentDecoder.decodeFrame();
        if (firstFrame) {
          return firstFrame.data;
        }
        return Buffer.alloc(0);
      }
    } catch (err: any) {
      console.error("[Preload] Failed to decode frame:", err);
      if (currentDecoder) {
        try {
          //currentDecoder.close();
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
