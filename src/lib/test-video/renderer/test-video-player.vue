<template>
    <div class="test-video-player">
        <div class="player-container">
            <canvas ref="canvasRef" class="video-canvas" :class="{ 'is-playing': isPlaying }"></canvas>

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
            <button @click="openDevTools" class="btn-secondary">
                🔧 开发者工具
            </button>

            <div class="config">
                <label>
                    分辨率:
                    <select v-model="resolution" :disabled="isPlaying">
                        <option value="7680x800">7680x800 (Full HD)</option>
                        <option value="2560x1440">2560x1440 (2K)</option>
                        <option value="3840x2160">3840x2160 (4K)</option>
                        <option value="6272x3456">6272x3456 (6K)</option>
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
                <span class="label">总耗时:</span>
                <span class="value" :class="{ 'warning': renderLatency > 33 }">
                    {{ renderLatency.toFixed(2) }}ms
                </span>
            </div>
            <div class="stat-row">
                <span class="label">渲染耗时:</span>
                <span class="value" :class="{ 'warning': actualRenderTime > 20 }">
                    {{ actualRenderTime.toFixed(2) }}ms
                </span>
            </div>
            <div class="stat-row">
                <span class="label">丢帧数:</span>
                <span class="value" :class="{ 'warning': droppedFrames > 0 }">
                    {{ droppedFrames }}
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
import { WebGLNV12Renderer } from "./webgl-nv12-renderer";

// 声明 window.testVideoAPI 类型（确保类型正确）
declare global {
    interface Window {
        testVideoAPI?: {
            start: (config: { width: number; height: number; fps: number }) => Promise<{ success: boolean; config: any }>;
            stop: () => void;
            notifyFrameRendered: () => void;
            getStats: () => Promise<any>;
            onFrameReady: (callback: (frameInfo: any) => void) => void;
            readFrameData: (shmName?: string) => Promise<number>;
            fillImageData: (buffer: Buffer, width: number, height: number) => Promise<void>;
            getImageData: (width: number, height: number) => Promise<Buffer>;
            getImageFromVideo: (width: number, height: number) => Buffer;
            getImageFromVideo2: (width: number, height: number) => Buffer;
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
const actualRenderTime = ref(0);  // 实际渲染耗时
const droppedFrames = ref(0);  // 丢帧计数

let renderer: WebGLNV12Renderer | null = null;
let fpsCounter = 0;
let lastFpsTime = Date.now();
let lastDataSize = 0;
let lastDataTime = Date.now();
let sharedMemoryName: string | undefined = undefined;  // 存储共享内存名称
let renderLoopId: number | null = null;  // requestAnimationFrame ID
let isRendering = false;  // 渲染中标志，防止并发渲染
let targetFps = 30;  // 目标帧率
let lastRenderTime = 0;  // 上次渲染时间

const frameSizeMB = computed(() => {
    // NV12 格式: 1.5 bytes per pixel (Y平面 + UV平面的一半)
    return (currentWidth.value * currentHeight.value * 1.5) / (1024 * 1024);
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
            renderer = new WebGLNV12Renderer(canvasRef.value);
            console.log("NV12 Renderer created successfully");
        } catch (err) {
            console.error("Failed to create renderer:", err);
        }
    }

    // 监听帧就绪事件
    if (window.testVideoAPI) {
        window.testVideoAPI.onFrameReady(() => { });
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
            sharedMemoryName = result.shmName;  // 保存共享内存名称
            targetFps = fps.value;  // 保存目标帧率
            lastRenderTime = performance.now();
            droppedFrames.value = 0;  // 重置丢帧计数

            // 启动渲染循环
            startRenderLoop(); console.log("Test video started:", result.config, "useSharedMemory:", result.useSharedMemory, "shmName:", result.shmName);
        }
    } catch (err) {
        console.error("Failed to start test video:", err);
        alert("启动失败！请确保 native addon 已编译。\n错误: " + err);
    }
}

function stop() {
    if (!isPlaying.value || !window.testVideoAPI) return;

    // 停止渲染循环
    stopRenderLoop();

    window.testVideoAPI.stop();
    isPlaying.value = false;
    currentFps.value = 0;

    if (renderer) {
        renderer.clear();
    }

    console.log("Test video stopped");
}

function openDevTools() {
    // @ts-ignore
    if (window.__ElectronUtils__) {
        // @ts-ignore
        window.__ElectronUtils__.openDevTools();
    }
}

// 启动渲染循环
function startRenderLoop() {
    const frameInterval = 1000 / targetFps;  // 帧间隔（ms）

    const renderLoop = async (currentTime: number) => {
        if (!isPlaying.value) return;

        // 控制帧率
        const elapsed = currentTime - lastRenderTime;
        if (elapsed >= frameInterval) {
            lastRenderTime = currentTime - (elapsed % frameInterval);

            // 如果上一帧还在渲染中，跳过本帧（防止积压）
            if (!isRendering) {
                await rendFrame({
                    width: currentWidth.value,
                    height: currentHeight.value,
                    frameNumber: frameNumber.value + 1
                });
            }
        }

        renderLoopId = requestAnimationFrame(renderLoop);
    };

    renderLoopId = requestAnimationFrame(renderLoop);
}

// 停止渲染循环
function stopRenderLoop() {
    if (renderLoopId !== null) {
        cancelAnimationFrame(renderLoopId);
        renderLoopId = null;
    }
}

async function rendFrame(frameInfo: any) {
    if (!renderer || !isPlaying.value || !window.testVideoAPI) return;

    // 防止并发渲染
    if (isRendering) {
        console.warn("Frame dropped: previous render still in progress");
        droppedFrames.value++;
        return;
    }

    isRendering = true;
    const startTime = performance.now();    // 第一帧时输出调试信息
    if (frameNumber.value === 0) {
        console.log("First frame info:", frameInfo);
    }

    try {
        // 读取帧数据 - 使用 VA-API 解码器从视频文件获取
        const readStart = performance.now();
        let buffer = await window.testVideoAPI.getImageFromVideo(frameInfo.width, frameInfo.height);
        const readTime = performance.now() - readStart;

        if (!buffer || buffer.length === 0) {
            console.error("No frame data received from video decoder");
            isRendering = false;
            return;
        }

        // 渲染
        const renderStart = performance.now();
        renderer.renderFrame(
            new Uint8Array(buffer as Buffer),
            frameInfo.width,
            frameInfo.height
        );
        const renderTime = performance.now() - renderStart;
        actualRenderTime.value = renderTime;  // 更新实际渲染耗时

        // 更新统计
        frameNumber.value = frameInfo.frameNumber;
        renderLatency.value = performance.now() - startTime;

        // 前10帧和每100帧输出性能统计
        if (frameNumber.value < 10 || frameNumber.value % 100 === 0) {
            console.log(`Frame ${frameNumber.value}: Read=${readTime.toFixed(2)}ms, Render=${renderTime.toFixed(2)}ms, Total=${renderLatency.value.toFixed(2)}ms, Dropped=${droppedFrames.value}, Size=${(buffer.byteLength / 1024 / 1024).toFixed(2)}MB`);
        }

        // 计算 FPS
        fpsCounter++;
        const now = Date.now();
        if (now - lastFpsTime >= 1000) {
            currentFps.value = fpsCounter;
            fpsCounter = 0;
            lastFpsTime = now;
        }

        // 计算吞吐量
        lastDataSize += buffer.byteLength;
        if (now - lastDataTime >= 1000) {
            lastDataTime = now;
            lastDataSize = 0;
        }

    } catch (err) {
        console.error("Failed to render frame:", err);
    } finally {
        isRendering = false;
    }
}
async function handleFrameReady(frameInfo: any) {

    if (!renderer || !isPlaying.value || !window.testVideoAPI) return;

    const startTime = performance.now();

    // 第一帧时输出调试信息
    if (frameNumber.value === 0) {
        console.log("First frame info:", frameInfo);
    }

    try {
        // 读取帧数据 - 使用保存的共享内存名称
        const readStart = performance.now();
        //const rtV = await window.testVideoAPI.readFrameData(sharedMemoryName);
        //let buffer = new Uint8Array(frameInfo.width * frameInfo.height * 3);
        let buffer = await window.testVideoAPI.getImageData(frameInfo.width, frameInfo.height);
        //await window.testVideoAPI.fillImageData(buffer, frameInfo.width, frameInfo.height);
        const readTime = performance.now() - readStart;

        if (!buffer) {
            console.error("No frame data received");
            // 通知主进程渲染完成（即使失败也要通知，避免卡住）
            window.testVideoAPI.notifyFrameRendered();
            return;
        }

        // 渲染
        const renderStart = performance.now();
        renderer.renderFrame(
            new Uint8Array(buffer as Buffer),
            frameInfo.width,
            frameInfo.height
        );
        const renderTime = performance.now() - renderStart;

        // 更新统计
        frameNumber.value = frameInfo.frameNumber;
        renderLatency.value = performance.now() - startTime;

        // 前10帧和每100帧输出性能统计
        if (frameNumber.value < 10 || frameNumber.value % 10 === 0) {
            console.log(`Frame ${frameNumber.value}: Read=${readTime.toFixed(2)}ms, Render=${renderTime.toFixed(2)}ms, Total=${renderLatency.value.toFixed(2)}ms, Size=${(buffer.byteLength / 1024 / 1024).toFixed(2)}MB`);
        }

        // 计算 FPS
        fpsCounter++;
        const now = Date.now();
        if (now - lastFpsTime >= 1000) {
            currentFps.value = fpsCounter;
            fpsCounter = 0;
            lastFpsTime = now;
        }

        // 计算吞吐量
        lastDataSize += buffer.byteLength;
        if (now - lastDataTime >= 1000) {
            lastDataTime = now;
            lastDataSize = 0;
        }

    } catch (err) {
        console.error("Failed to render frame:", err);
    } finally {
        // 通知主进程此帧已处理完成，可以发送下一帧
        window.testVideoAPI.notifyFrameRendered();
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

.btn-secondary {
    background: #6c757d;
    color: white;
}

.btn-secondary:hover:not(:disabled) {
    background: #5a6268;
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
