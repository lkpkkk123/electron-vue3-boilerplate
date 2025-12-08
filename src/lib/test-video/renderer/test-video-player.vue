<template>
  <div class="test-video-player">
    <div class="player-container">
      <canvas 
        ref="canvasRef" 
        class="video-canvas"
        :class="{ 'is-playing': isPlaying }"
      ></canvas>
      
      <div v-if="!isPlaying" class="overlay">
        <div class="message">点击"开始"按钮启动测试视频</div>
      </div>
    </div>
    
    <div class="controls">
      <button @click="start" :disabled="isPlaying" class="btn-primary">
        <span v-if="!isPlaying">▶ 开始</span>
        <span v-else>运行中...</span>
      </button>
      <button @click="stop" :disabled="!isPlaying" class="btn-danger">
        ⏹ 停止
      </button>
      
      <div class="config">
        <label>
          分辨率:
          <select v-model="resolution" :disabled="isPlaying">
            <option value="1920x1080">1920x1080 (Full HD)</option>
            <option value="2560x1440">2560x1440 (2K)</option>
            <option value="3840x2160">3840x2160 (4K)</option>
            <option value="7680x4320">7680x4320 (8K)</option>
          </select>
        </label>
        
        <label>
          帧率:
          <select v-model="fps" :disabled="isPlaying">
            <option :value="24">24 fps</option>
            <option :value="30">30 fps</option>
            <option :value="60">60 fps</option>
          </select>
        </label>
      </div>
    </div>
    
    <div class="stats">
      <div class="stat-row">
        <span class="label">分辨率:</span>
        <span class="value">{{ currentWidth }}x{{ currentHeight }}</span>
      </div>
      <div class="stat-row">
        <span class="label">当前 FPS:</span>
        <span class="value">{{ currentFps }}</span>
      </div>
      <div class="stat-row">
        <span class="label">帧号:</span>
        <span class="value">{{ frameNumber }}</span>
      </div>
      <div class="stat-row">
        <span class="label">渲染延迟:</span>
        <span class="value" :class="{ 'warning': renderLatency > 16 }">
          {{ renderLatency.toFixed(2) }}ms
        </span>
      </div>
      <div class="stat-row">
        <span class="label">帧大小:</span>
        <span class="value">{{ frameSizeMB.toFixed(2) }} MB</span>
      </div>
      <div class="stat-row">
        <span class="label">数据传输:</span>
        <span class="value">{{ dataThroughput.toFixed(2) }} MB/s</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from "vue";
import { WebGLRGBRenderer } from "./webgl-rgb-renderer";

// 声明 window.testVideoAPI 类型（确保类型正确）
declare global {
  interface Window {
    testVideoAPI?: {
      start: (config: { width: number; height: number; fps: number }) => Promise<{ success: boolean; config: any }>;
      stop: () => void;
      getStats: () => Promise<any>;
      onFrameReady: (callback: (frameInfo: any) => void) => void;
      readFrameData: (shmName?: string) => Promise<ArrayBuffer>;
    };
  }
}

const canvasRef = ref<HTMLCanvasElement | null>(null);
const isPlaying = ref(false);
const resolution = ref("1920x1080");
const fps = ref(30);

// 统计信息
const frameNumber = ref(0);
const currentWidth = ref(0);
const currentHeight = ref(0);
const currentFps = ref(0);
const renderLatency = ref(0);

let renderer: WebGLRGBRenderer | null = null;
let fpsCounter = 0;
let lastFpsTime = Date.now();
let lastDataSize = 0;
let lastDataTime = Date.now();

const frameSizeMB = computed(() => {
  return (currentWidth.value * currentHeight.value * 3) / (1024 * 1024);
});

const dataThroughput = computed(() => {
  const now = Date.now();
  const timeDiff = (now - lastDataTime) / 1000;
  if (timeDiff > 0) {
    return lastDataSize / timeDiff / (1024 * 1024);
  }
  return 0;
});

onMounted(() => {
  if (canvasRef.value) {
    try {
      renderer = new WebGLRGBRenderer(canvasRef.value);
      console.log("Renderer created successfully");
    } catch (err) {
      console.error("Failed to create renderer:", err);
    }
  }
  
  // 监听帧就绪事件
  if (window.testVideoAPI) {
    window.testVideoAPI.onFrameReady(handleFrameReady);
  }
});

onUnmounted(() => {
  stop();
  if (renderer) {
    renderer.dispose();
    renderer = null;
  }
});

async function start() {
  if (isPlaying.value || !window.testVideoAPI) return;
  
  const [width, height] = resolution.value.split("x").map(Number);
  
  try {
    const result = await window.testVideoAPI.start({
      width,
      height,
      fps: fps.value,
    });
    
    if (result.success) {
      isPlaying.value = true;
      currentWidth.value = width;
      currentHeight.value = height;
      frameNumber.value = 0;
      fpsCounter = 0;
      lastFpsTime = Date.now();
      lastDataTime = Date.now();
      lastDataSize = 0;
      
      console.log("Test video started:", result.config);
    }
  } catch (err) {
    console.error("Failed to start test video:", err);
    alert("启动失败！请确保 native addon 已编译。\n错误: " + err);
  }
}

function stop() {
  if (!isPlaying.value || !window.testVideoAPI) return;
  
  window.testVideoAPI.stop();
  isPlaying.value = false;
  currentFps.value = 0;
  
  if (renderer) {
    renderer.clear();
  }
  
  console.log("Test video stopped");
}

async function handleFrameReady(frameInfo: any) {
  if (!renderer || !isPlaying.value || !window.testVideoAPI) return;
  
  const startTime = performance.now();
  
  try {
    // 读取帧数据 - 优先使用共享内存
    const arrayBuffer = await window.testVideoAPI.readFrameData(frameInfo.shmName);
    
    if (!arrayBuffer) {
      console.error("No frame data received");
      return;
    }
    
    // 渲染
    renderer.renderFrame(
      new Uint8Array(arrayBuffer),
      frameInfo.width,
      frameInfo.height
    );
    
    // 更新统计
    frameNumber.value = frameInfo.frameNumber;
    renderLatency.value = performance.now() - startTime;
    
    // 计算 FPS
    fpsCounter++;
    const now = Date.now();
    if (now - lastFpsTime >= 1000) {
      currentFps.value = fpsCounter;
      fpsCounter = 0;
      lastFpsTime = now;
    }
    
    // 计算吞吐量
    lastDataSize += arrayBuffer.byteLength;
    if (now - lastDataTime >= 1000) {
      lastDataTime = now;
      lastDataSize = 0;
    }
    
  } catch (err) {
    console.error("Failed to render frame:", err);
  }
}
</script>

<style scoped>
.test-video-player {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 20px;
  max-width: 1400px;
  margin: 0 auto;
}

.player-container {
  position: relative;
  background: #000;
  border-radius: 8px;
  overflow: hidden;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
}

.video-canvas {
  display: block;
  max-width: 100%;
  max-height: 70vh;
  margin: 0 auto;
  background: #000;
}

.video-canvas.is-playing {
  cursor: default;
}

.overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.7);
  color: #fff;
  font-size: 18px;
}

.controls {
  display: flex;
  gap: 15px;
  align-items: center;
  padding: 15px;
  background: #f5f5f5;
  border-radius: 8px;
  flex-wrap: wrap;
}

.controls button {
  padding: 10px 20px;
  border: none;
  border-radius: 6px;
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
  min-width: 100px;
}

.controls button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: #007bff;
  color: white;
}

.btn-primary:hover:not(:disabled) {
  background: #0056b3;
}

.btn-danger {
  background: #dc3545;
  color: white;
}

.btn-danger:hover:not(:disabled) {
  background: #c82333;
}

.config {
  display: flex;
  gap: 15px;
  align-items: center;
  margin-left: auto;
}

.config label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 14px;
  font-weight: 500;
}

.config select {
  padding: 6px 12px;
  border: 1px solid #ddd;
  border-radius: 4px;
  font-size: 14px;
  background: white;
}

.stats {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
  padding: 15px;
  background: #f9f9f9;
  border-radius: 8px;
  font-family: 'Courier New', monospace;
  font-size: 14px;
}

.stat-row {
  display: flex;
  justify-content: space-between;
  padding: 8px 12px;
  background: white;
  border-radius: 4px;
  border-left: 3px solid #007bff;
}

.stat-row .label {
  color: #666;
  font-weight: 600;
}

.stat-row .value {
  color: #333;
  font-weight: 700;
}

.stat-row .value.warning {
  color: #ff6b6b;
}
</style>
