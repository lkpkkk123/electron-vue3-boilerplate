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
  private imageBuffer: Buffer | Uint8Array;  // 可以是 Buffer 或共享内存视图
  private frameCount: number = 0;
  private intervalId: NodeJS.Timeout | null = null;
  private isRunning: boolean = false;
  private useSharedMemory: boolean = false;
  private currentRow: number = 0;  // 当前修改的行
  private currentColor: number = 0; // 当前颜色阶段 (0=红, 1=蓝, 2=绿)
  private isRendering: boolean = false; // 渲染进程是否正在处理帧
  private targetWindow: BrowserWindow | null = null;

  constructor(config: TestImageConfig) {
    this.config = config;
    
    // 计算图像大小 (RGB 24bit)
    const imageSize = config.width * config.height * 3;
    
    console.log(`[TestImageGenerator] Initializing with ${config.width}x${config.height}, size=${imageSize} bytes`);
    console.log(`[TestImageGenerator] sharedMemory available: ${!!sharedMemory}`);
    
    // 创建共享内存
    if (sharedMemory) {
      try {
        const result = sharedMemory.create(this.shm_name, imageSize);
        console.log("✅ Shared memory created:", result);
        
        // 创建一个本地 Buffer, 我们会用 write() 方法写入共享内存
        this.imageBuffer = Buffer.alloc(imageSize);
        this.useSharedMemory = true;
        
        console.log("✅ Using shared memory with write() method, size:", imageSize);
      } catch (err) {
        console.error("❌ Failed to create shared memory:", err);
        this.imageBuffer = Buffer.alloc(imageSize);
        this.useSharedMemory = false;
      }
    } else {
      console.warn("⚠️ Shared memory addon not available, using Buffer");
      this.imageBuffer = Buffer.alloc(imageSize);
    }
    
    // 初始化为黑色
    this.imageBuffer.fill(0);
    
    console.log(`[TestImageGenerator] useSharedMemory=${this.useSharedMemory}`);
  }

  /**
   * 生成测试图像 - 每帧只修改一行
   * 每行从左到右颜色逐渐加深
   * 红色 → 蓝色 → 绿色循环
   */
  private generateTestImage(): void {
    const { width, height } = this.config;
    
    // 计算当前行的起始偏移量 (每像素 3 字节 RGB)
    const rowOffset = this.currentRow * width * 3;
    
    // 确定当前使用的颜色通道
    let colorIndex = this.currentColor; // 0=R, 1=G, 2=B
    
    // 修改当前行的所有像素
    for (let x = 0; x < width; x++) {
      const pixelOffset = rowOffset + x * 3;
      
      // 从左到右颜色逐渐加深 (0-255)
      const intensity = Math.floor((x / width) * 255);
      
      // 根据当前颜色阶段设置 RGB 值
      if (colorIndex === 0) {
        // 红色阶段
        this.imageBuffer[pixelOffset] = intensity;     // R
        this.imageBuffer[pixelOffset + 1] = 0;         // G
        this.imageBuffer[pixelOffset + 2] = 0;         // B
      } else if (colorIndex === 1) {
        // 蓝色阶段
        this.imageBuffer[pixelOffset] = 0;             // R
        this.imageBuffer[pixelOffset + 1] = 0;         // G
        this.imageBuffer[pixelOffset + 2] = intensity; // B
      } else {
        // 绿色阶段
        this.imageBuffer[pixelOffset] = 0;             // R
        this.imageBuffer[pixelOffset + 1] = intensity; // G
        this.imageBuffer[pixelOffset + 2] = 0;         // B
      }
    }
    
    // 移动到下一行
    this.currentRow++;
    
    // 如果所有行都已填充完当前颜色，切换到下一个颜色
    if (this.currentRow >= height) {
      this.currentRow = 0;
      this.currentColor = (this.currentColor + 1) % 3; // 0->1->2->0 循环
    }
    
    this.frameCount++;
  }

  /**
   * 写入共享内存
   */
  private writeToSharedMemory(): void {
    if (this.useSharedMemory && sharedMemory) {
      try {
        // 将 Buffer 数据写入共享内存
        sharedMemory.write(this.shm_name, this.imageBuffer as Buffer);
      } catch (err) {
        console.error("❌ Failed to write to shared memory:", err);
        // 写入失败,降级到 IPC 模式
        this.useSharedMemory = false;
      }
    }
  }

  /**
   * 获取当前帧数据 (用于 IPC 回退)
   */
  public getFrameData(): Buffer {
    if (Buffer.isBuffer(this.imageBuffer)) {
      return this.imageBuffer;
    }
    // 如果是 Uint8Array，转换为 Buffer
    return Buffer.from(this.imageBuffer);
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
    this.targetWindow = targetWindow || null;
    const interval = 1000 / this.config.fps;

    this.intervalId = setInterval(() => {
      // 只有在渲染进程准备好时才发送新帧
      if (this.isRendering) {
        // 跳过此帧，避免堆积
        return;
      }
      
      // 生成新帧
      this.generateTestImage();
      
      // 写入共享内存（如果可用）
      this.writeToSharedMemory();
      
      // 标记正在渲染
      this.isRendering = true;
      
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
   * 渲染进程通知帧已处理完成
   */
  public onFrameRendered(): void {
    this.isRendering = false;
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
  console.log("🔥🔥🔥 registerTestImageIPC called! 🔥🔥🔥");
  
  // 启动测试图像生成
  ipcMain.handle("test-image-start", async (event, config: TestImageConfig) => {
    console.log("🚀🚀🚀 test-image-start handler called! 🚀🚀🚀", config);
    
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
      useSharedMemory: testImageGenerator['useSharedMemory'],
      shmName: testImageGenerator['useSharedMemory'] ? testImageGenerator['shm_name'] : undefined,
    };
  });

  // 停止生成
  ipcMain.on("test-image-stop", () => {
    if (testImageGenerator) {
      testImageGenerator.stop();
    }
  });
  
  // 渲染进程通知帧已处理完成
  ipcMain.on("test-image-frame-rendered", () => {
    if (testImageGenerator) {
      testImageGenerator.onFrameRendered();
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
