<template>
    <div class="video-decoder-demo">
        <div class="controls">
            <h2>VA-API 视频解码器演示</h2>
            
            <div class="file-select">
                <input 
                    type="text" 
                    v-model="videoPath" 
                    placeholder="输入视频文件路径 (H264/H265)"
                    class="path-input"
                />
                <button @click="selectFile" class="btn-secondary">选择文件</button>
            </div>

            <div class="action-buttons">
                <button @click="initDecoder" :disabled="isDecoding || !videoPath" class="btn-primary">
                    初始化解码器
                </button>
                <button @click="startDecoding" :disabled="!decoderReady || isDecoding" class="btn-primary">
                    开始解码
                </button>
                <button @click="stopDecoding" :disabled="!isDecoding" class="btn-danger">
                    停止
                </button>
            </div>

            <div v-if="videoInfo" class="video-info">
                <h3>视频信息</h3>
                <div class="info-row">
                    <span class="label">分辨率:</span>
                    <span class="value">{{ videoInfo.width }}x{{ videoInfo.height }}</span>
                </div>
                <div class="info-row">
                    <span class="label">编码:</span>
                    <span class="value">{{ videoInfo.codec }}</span>
                </div>
                <div class="info-row">
                    <span class="label">帧率:</span>
                    <span class="value">{{ videoInfo.fps.toFixed(2) }} fps</span>
                </div>
            </div>

            <div class="stats">
                <div class="stat-row">
                    <span class="label">当前帧:</span>
                    <span class="value">{{ currentFrame }}</span>
                </div>
                <div class="stat-row">
                    <span class="label">解码 FPS:</span>
                    <span class="value">{{ decodeFps.toFixed(2) }}</span>
                </div>
                <div class="stat-row">
                    <span class="label">解码耗时:</span>
                    <span class="value">{{ decodeTime.toFixed(2) }}ms</span>
                </div>
            </div>
        </div>

        <div class="player-container">
            <canvas ref="canvasRef" class="video-canvas"></canvas>
        </div>
    </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import { WebGLNV12Renderer } from '../../test-video/renderer/webgl-nv12-renderer';

declare global {
    interface Window {
        videoDecoderAPI?: {
            initFromFile: (filename: string) => Promise<boolean>;
            decodeFrame: () => Promise<{ data: Buffer; width: number; height: number; format: string } | null>;
            getVideoInfo: () => Promise<{ width: number; height: number; codec: string; fps: number } | null>;
            close: () => void;
        };
    }
}

const canvasRef = ref<HTMLCanvasElement | null>(null);
const videoPath = ref('/path/to/video.h264');
const decoderReady = ref(false);
const isDecoding = ref(false);
const currentFrame = ref(0);
const decodeFps = ref(0);
const decodeTime = ref(0);

const videoInfo = ref<{ width: number; height: number; codec: string; fps: number } | null>(null);

let renderer: WebGLNV12Renderer | null = null;
let decodeLoopId: number | null = null;
let fpsCounter = 0;
let lastFpsTime = Date.now();

onMounted(() => {
    if (canvasRef.value) {
        try {
            renderer = new WebGLNV12Renderer(canvasRef.value);
            console.log('NV12 Renderer initialized for decoder');
        } catch (err) {
            console.error('Failed to create renderer:', err);
        }
    }
});

onUnmounted(() => {
    stopDecoding();
    if (renderer) {
        renderer.dispose();
        renderer = null;
    }
});

async function selectFile() {
    // TODO: 实现文件选择对话框
    alert('请在输入框中直接输入文件路径');
}

async function initDecoder() {
    if (!window.videoDecoderAPI) {
        alert('Video Decoder API 未加载');
        return;
    }

    try {
        const success = await window.videoDecoderAPI.initFromFile(videoPath.value);
        
        if (success) {
            decoderReady.value = true;
            
            // 获取视频信息
            const info = await window.videoDecoderAPI.getVideoInfo();
            if (info) {
                videoInfo.value = info;
                console.log('Video info:', info);
            }
            
            alert('解码器初始化成功！');
        } else {
            alert('解码器初始化失败！请检查文件路径和格式。');
        }
    } catch (err) {
        console.error('Init decoder error:', err);
        alert('初始化错误: ' + err);
    }
}

async function startDecoding() {
    if (!decoderReady.value || !window.videoDecoderAPI || !renderer) {
        return;
    }

    isDecoding.value = true;
    currentFrame.value = 0;
    fpsCounter = 0;
    lastFpsTime = Date.now();

    // 开始解码循环
    decodeLoop();
}

function stopDecoding() {
    isDecoding.value = false;
    
    if (decodeLoopId !== null) {
        cancelAnimationFrame(decodeLoopId);
        decodeLoopId = null;
    }

    if (window.videoDecoderAPI) {
        window.videoDecoderAPI.close();
        decoderReady.value = false;
        videoInfo.value = null;
    }
}

async function decodeLoop() {
    if (!isDecoding.value || !window.videoDecoderAPI || !renderer) {
        return;
    }

    try {
        const startTime = performance.now();
        
        // 解码一帧
        const frame = await window.videoDecoderAPI.decodeFrame();
        
        if (frame) {
            // 渲染帧
            renderer.renderFrame(
                new Uint8Array(frame.data as Buffer),
                frame.width,
                frame.height
            );

            currentFrame.value++;
            decodeTime.value = performance.now() - startTime;

            // 更新 FPS
            fpsCounter++;
            const now = Date.now();
            if (now - lastFpsTime >= 1000) {
                decodeFps.value = (fpsCounter * 1000) / (now - lastFpsTime);
                fpsCounter = 0;
                lastFpsTime = now;
            }

            // 继续解码下一帧
            decodeLoopId = requestAnimationFrame(decodeLoop);
        } else {
            // 解码完成
            console.log('Decoding finished');
            isDecoding.value = false;
            alert('视频解码完成！');
        }
    } catch (err) {
        console.error('Decode error:', err);
        isDecoding.value = false;
        alert('解码错误: ' + err);
    }
}
</script>

<style scoped>
.video-decoder-demo {
    display: flex;
    flex-direction: column;
    height: 100vh;
    background: #1e1e1e;
    color: #fff;
    padding: 20px;
}

.controls {
    margin-bottom: 20px;
}

h2 {
    margin: 0 0 20px 0;
    color: #4fc3f7;
}

.file-select {
    display: flex;
    gap: 10px;
    margin-bottom: 15px;
}

.path-input {
    flex: 1;
    padding: 10px;
    background: #2d2d2d;
    border: 1px solid #444;
    color: #fff;
    border-radius: 4px;
    font-size: 14px;
}

.action-buttons {
    display: flex;
    gap: 10px;
    margin-bottom: 20px;
}

.btn-primary, .btn-secondary, .btn-danger {
    padding: 10px 20px;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-size: 14px;
    transition: all 0.3s;
}

.btn-primary {
    background: #4fc3f7;
    color: #000;
}

.btn-primary:hover:not(:disabled) {
    background: #29b6f6;
}

.btn-secondary {
    background: #666;
    color: #fff;
}

.btn-secondary:hover:not(:disabled) {
    background: #777;
}

.btn-danger {
    background: #f44336;
    color: #fff;
}

.btn-danger:hover:not(:disabled) {
    background: #e53935;
}

button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.video-info, .stats {
    background: #2d2d2d;
    padding: 15px;
    border-radius: 4px;
    margin-bottom: 15px;
}

.video-info h3 {
    margin: 0 0 10px 0;
    color: #4fc3f7;
    font-size: 16px;
}

.info-row, .stat-row {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid #444;
}

.info-row:last-child, .stat-row:last-child {
    border-bottom: none;
}

.label {
    color: #aaa;
    font-weight: 500;
}

.value {
    color: #fff;
    font-family: monospace;
}

.player-container {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    background: #000;
    border-radius: 4px;
    overflow: hidden;
}

.video-canvas {
    max-width: 100%;
    max-height: 100%;
    object-fit: contain;
}
</style>
