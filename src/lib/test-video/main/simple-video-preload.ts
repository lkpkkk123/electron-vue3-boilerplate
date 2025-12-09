/**
 * 简化版 Preload 脚本 - 使用 NAL 单元解析的解码器
 */
import { ipcRenderer } from "electron";
import path from "path";
import fs from "fs";

// 动态加载解码器 addon
let SimpleVaapiDecoder: any = null;
let currentDecoder: any = null;

try {
  const addonPath = path.join(__dirname, "../../../../native/vaapi-decoder/build/Release/simple_vaapi_decoder.node");
  
  if (fs.existsSync(addonPath)) {
    const addon = require(addonPath);
    SimpleVaapiDecoder = addon.SimpleVaapiDecoder;
    console.log("[Preload] Simple VA-API decoder loaded successfully");
  } else {
    console.error("[Preload] Decoder addon not found at:", addonPath);
  }
} catch (err) {
  console.error("[Preload] Failed to load simple VA-API decoder addon:", err);
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

  /**
   * 从视频解码获取图像数据
   * 返回 NV12 格式的数据
   */
  getImageFromVideo: (width: number, height: number): Buffer => {
    if (!SimpleVaapiDecoder) {
      console.error("[Preload] Simple VA-API decoder not available");
      return Buffer.alloc(0);
    }

    try {
      // 如果解码器未初始化，则初始化
      if (!currentDecoder) {
        console.log("[Preload] Initializing simple VA-API decoder...");
        currentDecoder = new SimpleVaapiDecoder();
        
        const videoPath = "/home/likp/Public/osd2.mp4";
        
        // 检查文件是否存在
        if (!fs.existsSync(videoPath)) {
          console.error("[Preload] Video file does not exist:", videoPath);
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        
        const fileSize = fs.statSync(videoPath).size;
        console.log("[Preload] Video file exists, size:", fileSize, "bytes");
        
        // 检测编码格式（简单检测，这里假设是 H265）
        // 实际应该读取文件头部或者用户指定
        const codec = 'hevc'; // 或 'h264'
        
        console.log("[Preload] Initializing decoder with codec:", codec);
        const success = currentDecoder.init(videoPath, codec);
        
        if (!success) {
          const error = currentDecoder.getLastError();
          console.error("[Preload] Failed to initialize decoder:", error);
          currentDecoder = null;
          return Buffer.alloc(0);
        }
        
        console.log("[Preload] Decoder initialized successfully");
        
        // 获取视频信息
        const videoInfo = currentDecoder.getVideoInfo();
        if (videoInfo) {
          console.log("[Preload] Video info:", videoInfo);
        }
      }
      
      // 解码一帧
      const frame = currentDecoder.decodeFrame();
      
      if (!frame) {
        console.log("[Preload] End of video reached, resetting...");
        // 重置到开头循环播放
        currentDecoder.reset();
        
        // 解码第一帧
        const firstFrame = currentDecoder.decodeFrame();
        if (!firstFrame) {
          console.error("[Preload] Failed to decode first frame after reset");
          return Buffer.alloc(0);
        }
        
        console.log("[Preload] Decoded first frame after reset:", 
                    firstFrame.width, "x", firstFrame.height, 
                    "format:", firstFrame.format);
        return firstFrame.data;
      }
      
      // 返回 NV12 数据
      return frame.data;
      
    } catch (err: any) {
      console.error("[Preload] Error in getImageFromVideo:", err);
      if (currentDecoder) {
        try {
          const error = currentDecoder.getLastError();
          console.error("[Preload] Decoder error:", error);
        } catch (e) {
          // ignore
        }
      }
      return Buffer.alloc(0);
    }
  },
};
