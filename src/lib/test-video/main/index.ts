/**
 * 测试图像生成器 - 主进程
 * 生成动态彩色图像并写入共享内存
 */
import { BrowserWindow, ipcMain } from "electron";
import path from "path";

// 动态加载 native addon
let sharedMemory: any = null;

try {
  // 从 build/lib/test-video/main/ 访问 native/shared-memory/build/Release/
  // 需要向上4级到项目根目录
  const addonPath = path.join(__dirname, "../../../../native/shared-memory/build/Release/shared_memory.node");
  sharedMemory = require(addonPath);
  console.log("✅ Shared memory addon loaded in main process");
} catch (err) {
  console.error("❌ Failed to load shared memory addon:", err);
  console.log("Will use IPC for data transfer");
}

interface TestImageConfig {
  width: number;
  height: number;
  fps: number;
}

class TestImageGenerator {
  private config: TestImageConfig;
  private shm_name: string = "/test_video_shm";
  private imageBuffer: Buffer;
  private frameCount: number = 0;
  private intervalId: NodeJS.Timeout | null = null;
  private isRunning: boolean = false;
  private useSharedMemory: boolean = false;

  constructor(config: TestImageConfig) {
    this.config = config;
    
    // 计算图像大小 (RGB 24bit)
    const imageSize = config.width * config.height * 3;
    this.imageBuffer = Buffer.alloc(imageSize);
    
    // 尝试创建共享内存
    if (sharedMemory) {
      try {
        const result = sharedMemory.create(this.shm_name, imageSize);
        console.log("Shared memory created:", result);
        this.useSharedMemory = true;
      } catch (err) {
        console.error("Failed to create shared memory:", err);
        this.useSharedMemory = false;
      }
    }
  }

  /**
   * 生成测试图像 - RGB 数据
   * 红绿蓝三色渐变动画
   */
  private generateTestImage(): void {
    const { width, height } = this.config;
    const time = this.frameCount / 60.0; // 时间因子
    
    let offset = 0;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        // 计算归一化坐标
        const nx = x / width;
        const ny = y / height;
        
        // 生成动态颜色
        // 红色：左右渐变 + 时间变化
        const r = Math.floor(255 * Math.abs(Math.sin(nx * Math.PI + time)));
        
        // 绿色：上下渐变 + 时间变化
        const g = Math.floor(255 * Math.abs(Math.sin(ny * Math.PI + time * 0.7)));
        
        // 蓝色：对角渐变 + 时间变化
        const b = Math.floor(255 * Math.abs(Math.sin((nx + ny) * Math.PI + time * 0.5)));
        
        // 写入 RGB
        this.imageBuffer[offset++] = r;
        this.imageBuffer[offset++] = g;
        this.imageBuffer[offset++] = b;
      }
    }
    
    this.frameCount++;
  }

  /**
   * 写入共享内存
   */
  private writeToSharedMemory(): void {
    if (this.useSharedMemory && sharedMemory) {
      try {
        sharedMemory.write(this.shm_name, this.imageBuffer);
      } catch (err) {
        console.error("Failed to write to shared memory:", err);
      }
    }
  }

  /**
   * 获取当前帧数据 (用于 IPC 回退)
   */
  public getFrameData(): Buffer {
    return this.imageBuffer;
  }

  /**
   * 启动生成器
   */
  public start(targetWindow?: BrowserWindow): void {
    if (this.isRunning) {
      console.log("Generator already running");
      return;
    }

    this.isRunning = true;
    const interval = 1000 / this.config.fps;

    this.intervalId = setInterval(() => {
      // 生成新帧
      this.generateTestImage();
      
      // 写入共享内存（如果可用）
      this.writeToSharedMemory();
      
      // 通知渲染进程
      const frameInfo = {
        shmName: this.useSharedMemory ? this.shm_name : undefined,
        width: this.config.width,
        height: this.config.height,
        frameNumber: this.frameCount,
        timestamp: Date.now(),
        useSharedMemory: this.useSharedMemory,
      };
      
      if (targetWindow && !targetWindow.isDestroyed()) {
        targetWindow.webContents.send("test-frame-ready", frameInfo);
      } else {
        // 广播给所有窗口
        BrowserWindow.getAllWindows().forEach(win => {
          if (!win.isDestroyed()) {
            win.webContents.send("test-frame-ready", frameInfo);
          }
        });
      }
    }, interval);

    console.log(`Test image generator started: ${this.config.width}x${this.config.height} @ ${this.config.fps}fps (${this.useSharedMemory ? 'Shared Memory' : 'IPC'})`);
  }

  /**
   * 停止生成器
   */
  public stop(): void {
    if (this.intervalId) {
      clearInterval(this.intervalId);
      this.intervalId = null;
    }
    this.isRunning = false;
    console.log("Test image generator stopped");
  }

  /**
   * 清理资源
   */
  public dispose(): void {
    this.stop();
    
    if (this.useSharedMemory && sharedMemory) {
      try {
        sharedMemory.close(this.shm_name);
        console.log("Shared memory closed");
      } catch (err) {
        console.error("Failed to close shared memory:", err);
      }
    }
  }

  public getConfig(): TestImageConfig {
    return { ...this.config };
  }

  public getFrameCount(): number {
    return this.frameCount;
  }
}

// 全局实例
let testImageGenerator: TestImageGenerator | null = null;

/**
 * 注册 IPC 处理器
 */
export function registerTestImageIPC(): void {
  // 启动测试图像生成
  ipcMain.handle("test-image-start", async (event, config: TestImageConfig) => {
    if (testImageGenerator) {
      testImageGenerator.dispose();
    }

    // 默认配置
    const defaultConfig: TestImageConfig = {
      width: 1920,
      height: 1080,
      fps: 30,
    };

    const finalConfig = { ...defaultConfig, ...config };
    testImageGenerator = new TestImageGenerator(finalConfig);
    
    // 获取发送请求的窗口
    const win = BrowserWindow.fromWebContents(event.sender);
    testImageGenerator.start(win || undefined);

    return {
      success: true,
      config: finalConfig,
    };
  });

  // 停止生成
  ipcMain.on("test-image-stop", () => {
    if (testImageGenerator) {
      testImageGenerator.stop();
    }
  });

  // 获取统计信息
  ipcMain.handle("test-image-stats", async () => {
    if (testImageGenerator) {
      return {
        frameCount: testImageGenerator.getFrameCount(),
        config: testImageGenerator.getConfig(),
      };
    }
    return null;
  });

  // 获取当前帧数据
  ipcMain.handle("test-image-get-frame-data", async () => {
    if (testImageGenerator) {
      const buffer = testImageGenerator.getFrameData();
      // 返回 ArrayBuffer
      return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
    }
    return null;
  });
}

/**
 * 清理函数 - 应用退出时调用
 */
export function cleanupTestImage(): void {
  if (testImageGenerator) {
    testImageGenerator.dispose();
    testImageGenerator = null;
  }
}
