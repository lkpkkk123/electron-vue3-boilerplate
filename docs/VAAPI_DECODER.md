# VA-API 硬件视频解码器

基于 VA-API 的硬件加速视频解码器，支持 H264/H265 解码，输出 NV12 格式。

## 特性

- ✅ **硬件加速**: 使用 VA-API 进行 GPU 解码
- ✅ **多格式支持**: H264, H265/HEVC
- ✅ **NV12 输出**: 直接输出 NV12 格式，可与 WebGL 渲染器无缝集成
- ✅ **零拷贝**: 硬件解码直接到系统内存
- ✅ **流式解码**: 支持文件和内存流解码

## 系统要求

### Linux
- VA-API 驱动 (Intel/AMD/NVIDIA)
- FFmpeg 4.0+
- node-gyp
- C++14 编译器

### 安装依赖

**Ubuntu/Debian:**
```bash
sudo apt-get install libva-dev libva-drm2 \
    libavcodec-dev libavformat-dev libavutil-dev \
    build-essential
```

**Fedora:**
```bash
sudo dnf install libva-devel ffmpeg-devel gcc-c++
```

**Arch Linux:**
```bash
sudo pacman -S libva ffmpeg base-devel
```

### 验证 VA-API
```bash
# 安装 vainfo 工具
sudo apt-get install vainfo

# 检查 VA-API 支持
vainfo

# 应该看到类似输出:
# VAProfileH264Main               : VAEntrypointVLD
# VAProfileH264High               : VAEntrypointVLD
# VAProfileHEVCMain               : VAEntrypointVLD
```

## 编译

```bash
# 运行编译脚本
chmod +x scripts/build-vaapi-decoder.sh
./scripts/build-vaapi-decoder.sh

# 或手动编译
cd native/vaapi-decoder
npm install
npm run build
```

## 使用方法

### 1. 从文件解码

```typescript
import { VaapiDecoder } from '@/lib/video-decoder/main/vaapi-decoder';

const decoder = new VaapiDecoder();

// 初始化
if (decoder.initFromFile('/path/to/video.h264')) {
    // 获取视频信息
    const info = decoder.getVideoInfo();
    console.log(`${info.width}x${info.height} @ ${info.fps} fps, codec: ${info.codec}`);

    // 解码帧
    let frame;
    while ((frame = decoder.decodeFrame()) !== null) {
        console.log(`Frame: ${frame.width}x${frame.height}, size: ${frame.data.length}`);
        // frame.data 是 NV12 格式的 Buffer
    }

    decoder.close();
}
```

### 2. 流式解码

```typescript
import { StreamDecoder } from '@/lib/video-decoder/main/vaapi-decoder';

const decoder = new StreamDecoder('h264_vaapi', (frame, frameNumber) => {
    console.log(`Frame ${frameNumber}: ${frame.width}x${frame.height}`);
    // 处理解码后的帧
});

// 推送 H264 数据包
decoder.push(h264Packet1);
decoder.push(h264Packet2);
// ...

decoder.close();
```

### 3. 在渲染进程中使用

```typescript
// 使用 preload 脚本暴露的 API
const api = window.videoDecoderAPI;

// 初始化
await api.initFromFile('/path/to/video.mp4');

// 获取信息
const info = await api.getVideoInfo();

// 解码帧
const frame = await api.decodeFrame();
if (frame) {
    // 使用 WebGL NV12 渲染器显示
    renderer.renderFrame(
        new Uint8Array(frame.data),
        frame.width,
        frame.height
    );
}

// 关闭
api.close();
```

### 4. 辅助函数

```typescript
import { decodeVideoFile } from '@/lib/video-decoder/main/vaapi-decoder';

// 解码整个文件
await decodeVideoFile('/path/to/video.h264', (frame, frameNumber) => {
    console.log(`Processing frame ${frameNumber}`);
    // 处理帧...
});
```

## API 参考

### VaapiDecoder

#### 方法

- `initFromFile(filename: string): boolean`
  - 从文件初始化解码器
  - 返回是否成功

- `initFromBuffer(buffer: Buffer, codecName: string): boolean`
  - 从缓冲区初始化
  - codecName: 'h264_vaapi' 或 'hevc_vaapi'

- `decodeFrame(): DecodedFrame | null`
  - 解码一帧（从文件）
  - 返回帧数据或 null（文件结束）

- `decodePacket(packet: Buffer): DecodedFrame | null`
  - 解码数据包（从内存）
  - 返回帧数据或 null（需要更多数据）

- `getVideoInfo(): VideoInfo | null`
  - 获取视频信息

- `close(): void`
  - 关闭解码器，释放资源

#### 类型

```typescript
interface DecodedFrame {
  data: Buffer;      // NV12 格式数据
  width: number;     // 宽度
  height: number;    // 高度
  format: 'nv12';    // 像素格式
}

interface VideoInfo {
  width: number;     // 视频宽度
  height: number;    // 视频高度
  codec: string;     // 编码格式
  fps: number;       // 帧率
}
```

## 性能优化

### 硬件加速验证

```bash
# 确认 VA-API 正在使用
export LIBVA_DRIVER_NAME=iHD  # Intel
# 或
export LIBVA_DRIVER_NAME=radeonsi  # AMD

# 运行时查看日志
export LIBVA_MESSAGING_LEVEL=2
```

### 性能建议

1. **零拷贝传输**: 解码后的 NV12 数据可以直接传给 WebGL，无需格式转换
2. **批量处理**: 使用 `decodeFrame()` 循环解码比单帧调用更高效
3. **内存复用**: decoder 内部复用 buffer，减少内存分配

## 故障排除

### 1. VA-API 初始化失败

```
Error: Failed to create VA-API device context
```

**解决方案:**
- 检查 `/dev/dri/renderD128` 是否存在且可访问
- 确认用户在 `video` 或 `render` 组中: `sudo usermod -aG video $USER`
- 重新登录使组权限生效

### 2. 不支持的编码格式

```
Error: Failed to find decoder
```

**解决方案:**
- 运行 `vainfo` 检查支持的配置文件
- 某些 GPU 只支持 H264，不支持 HEVC
- 考虑使用软件解码作为备选

### 3. 编译错误

```
fatal error: va/va.h: No such file or directory
```

**解决方案:**
- 安装 VA-API 开发包: `sudo apt-get install libva-dev`
- 确认 pkg-config 能找到: `pkg-config --cflags libva`

## 示例项目

查看 `src/lib/video-decoder/renderer/video-decoder-demo.vue` 获取完整的使用示例。

## 技术架构

```
┌─────────────────┐
│   Vue 组件      │  (video-decoder-demo.vue)
└────────┬────────┘
         │
┌────────▼────────┐
│  Preload API    │  (video-decoder-preload.ts)
└────────┬────────┘
         │
┌────────▼────────┐
│ VaapiDecoder    │  (vaapi-decoder.ts)
│  TypeScript     │
└────────┬────────┘
         │
┌────────▼────────┐
│  Native Addon   │  (vaapi_decoder.cpp)
│   N-API C++     │
└────────┬────────┘
         │
    ┌────▼─────┬──────────┐
    │  VA-API  │  FFmpeg  │
    └──────────┴──────────┘
```

## 许可证

与项目主许可证相同
