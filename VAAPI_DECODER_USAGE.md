# VA-API 视频解码器使用指南

## 快速开始

### 1. 编译解码器

```bash
# 确保已安装依赖
sudo apt-get install libva-dev libva-drm2 libavcodec-dev libavformat-dev libavutil-dev

# 编译
./scripts/build-vaapi-decoder.sh
```

### 2. 测试解码器

```bash
# 使用测试脚本
node test-decoder.js /path/to/video.mp4

# 预期输出:
# ✓ VA-API decoder addon loaded successfully
# ✓ VaapiDecoder instance created
# 
# Video Information:
#   Resolution: 1920x1080
#   Codec: h264
#   FPS: 30.00
# 
# Decoding frames...
#   Frame 1: 1920x1080, 3.04 MB, format: nv12
#   Frame 2: 1920x1080, 3.04 MB, format: nv12
#   ...
```

### 3. 在代码中使用

#### 主进程 (Node.js)

```typescript
import { VaapiDecoder } from '@/lib/video-decoder/main/vaapi-decoder';

const decoder = new VaapiDecoder();

// 从文件解码
if (decoder.initFromFile('/path/to/video.h264')) {
    const info = decoder.getVideoInfo();
    console.log(`Video: ${info.width}x${info.height} @ ${info.fps} fps`);
    
    // 解码所有帧
    let frame;
    while ((frame = decoder.decodeFrame()) !== null) {
        // frame.data 是 NV12 格式的 Buffer
        processFrame(frame.data, frame.width, frame.height);
    }
    
    decoder.close();
}
```

#### 渲染进程 (通过 Preload)

```vue
<script setup lang="ts">
import { WebGLNV12Renderer } from '@/lib/test-video/renderer/webgl-nv12-renderer';

const decoder = window.videoDecoderAPI;

async function playVideo() {
    // 初始化解码器
    await decoder.initFromFile('/path/to/video.mp4');
    
    // 获取视频信息
    const info = await decoder.getVideoInfo();
    
    // 创建渲染器
    const renderer = new WebGLNV12Renderer(canvas);
    
    // 解码并渲染
    let frame;
    while ((frame = await decoder.decodeFrame()) !== null) {
        renderer.renderFrame(
            new Uint8Array(frame.data),
            frame.width,
            frame.height
        );
        
        // 控制帧率
        await new Promise(resolve => setTimeout(resolve, 1000 / info.fps));
    }
    
    decoder.close();
}
</script>
```

### 4. 流式解码示例

```typescript
import { StreamDecoder } from '@/lib/video-decoder/main/vaapi-decoder';

// 创建流解码器
const decoder = new StreamDecoder('h264_vaapi', (frame, frameNumber) => {
    console.log(`Frame ${frameNumber}: ${frame.width}x${frame.height}`);
    
    // 渲染或处理帧
    renderer.renderFrame(
        new Uint8Array(frame.data),
        frame.width,
        frame.height
    );
});

// 从网络或其他源接收 H264 数据包
socket.on('data', (packet: Buffer) => {
    decoder.push(packet);
});

// 完成后关闭
decoder.close();
```

## API 说明

### VaapiDecoder 类

```typescript
class VaapiDecoder {
    // 从文件初始化
    initFromFile(filename: string): boolean;
    
    // 从缓冲区初始化 (codecName: 'h264_vaapi' | 'hevc_vaapi')
    initFromBuffer(buffer: Buffer, codecName: string): boolean;
    
    // 解码一帧 (从文件)
    decodeFrame(): DecodedFrame | null;
    
    // 解码数据包 (从内存)
    decodePacket(packet: Buffer): DecodedFrame | null;
    
    // 获取视频信息
    getVideoInfo(): VideoInfo | null;
    
    // 关闭解码器
    close(): void;
}
```

### 数据类型

```typescript
interface DecodedFrame {
    data: Buffer;      // NV12 格式数据 (Y平面 + UV平面)
    width: number;     // 宽度
    height: number;    // 高度
    format: 'nv12';    // 像素格式
}

interface VideoInfo {
    width: number;     // 视频宽度
    height: number;    // 视频高度
    codec: string;     // 编码格式 (h264/hevc)
    fps: number;       // 帧率
}
```

## 性能特性

- **硬件加速**: 使用 GPU 解码，CPU 占用低
- **NV12 格式**: 1.5 字节/像素 (vs RGB 3 字节/像素)
- **零拷贝**: 直接从 GPU 到系统内存，无需额外转换
- **高效渲染**: NV12 可直接用于 WebGL 渲染

### 性能对比

| 分辨率 | RGB 大小 | NV12 大小 | 节省 |
|--------|----------|-----------|------|
| 1080p  | 6.2 MB   | 3.1 MB    | 50%  |
| 4K     | 23.7 MB  | 11.8 MB   | 50%  |
| 6K     | 64.8 MB  | 32.4 MB   | 50%  |

## 故障排除

### 1. 编译失败

**错误**: `fatal error: va/va.h: No such file or directory`

**解决**:
```bash
sudo apt-get install libva-dev libva-drm2
```

### 2. VA-API 初始化失败

**错误**: `Failed to create VA-API device context`

**解决**:
```bash
# 检查 VA-API 支持
vainfo

# 确认 DRM 设备存在
ls -l /dev/dri/renderD128

# 添加用户到 video 组
sudo usermod -aG video $USER
# 重新登录
```

### 3. 解码器不支持格式

**错误**: `Failed to find decoder`

**解决**:
```bash
# 检查支持的编码格式
vainfo | grep -E "VAProfile"

# 如果只支持 H264，HEVC 需要软件解码
```

### 4. 运行时错误

**调试方法**:
```bash
# 启用 VA-API 详细日志
export LIBVA_MESSAGING_LEVEL=2
export LIBVA_DRIVER_NAME=iHD  # Intel
# 或
export LIBVA_DRIVER_NAME=radeonsi  # AMD

# 运行程序查看日志
node test-decoder.js video.mp4
```

## 完整示例

查看以下文件获取完整示例:

- **测试脚本**: `test-decoder.js`
- **Vue 组件**: `src/lib/video-decoder/renderer/video-decoder-demo.vue`
- **详细文档**: `docs/VAAPI_DECODER.md`

## 许可证

与项目主许可证相同
