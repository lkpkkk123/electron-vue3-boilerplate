# 共享内存测试视频播放器 - 编译和运行指南

## 📦 项目说明

这是一个演示如何使用**共享内存 + WebGL**实现高性能视频渲染的示例项目。

### 技术架构
```
┌─────────────────────────────────────────┐
│  主进程 (Node.js)                        │
│  - 生成测试图像 (RGB动态渐变)             │
│  - 写入共享内存 (POSIX shm)              │
└──────────────┬──────────────────────────┘
               │ 共享内存 (Zero-Copy)
        ┌──────┴──────┐
        ▼             ▼
┌──────────────┐ ┌──────────────────────────┐
│ Native Addon │ │ 渲染进程 (Browser)        │
│ (C++ NAPI)   │ │ - WebGL RGB 渲染          │
│ - 读写共享内存│ │ - Canvas 显示             │
└──────────────┘ └──────────────────────────┘
```

### 性能特点
- ✅ 零拷贝数据传输
- ✅ 支持超大分辨率 (8K)
- ✅ WebGL GPU 加速渲染
- ✅ 可达 60+ FPS

## 🛠️ 编译步骤

### 1. 安装依赖

```bash
# 项目根目录
yarn install

# 进入 native addon 目录
cd native/shared-memory
npm install
```

### 2. 编译 Native Addon

#### Linux/macOS:
```bash
cd native/shared-memory
npm run install  # 会自动执行 node-gyp rebuild
```

#### 如果遇到编译错误:
```bash
# 确保安装了必要的构建工具
# Ubuntu/Debian:
sudo apt-get install build-essential python3

# macOS:
xcode-select --install

# 手动编译
node-gyp configure
node-gyp build
```

### 3. 验证编译结果

编译成功后应该在以下位置看到编译产物:
```
native/shared-memory/build/Release/shared_memory.node
```

## 🚀 运行

### 启动开发服务器
```bash
yarn run dev
```

### 使用步骤
1. 主窗口会自动打开
2. 点击 **"🎬 测试视频播放器 (共享内存)"** 按钮
3. 在新窗口中:
   - 选择分辨率 (1080p ~ 8K)
   - 选择帧率 (24/30/60 fps)
   - 点击 **"开始"** 按钮
4. 观察动态彩色渐变视频

## 📊 性能监控

播放器界面会实时显示:
- **当前 FPS**: 实际渲染帧率
- **渲染延迟**: 单帧渲染耗时
- **帧大小**: 每帧数据大小
- **数据传输**: 共享内存吞吐量

### 性能基准 (参考)
| 分辨率 | 帧大小 | 预期FPS | 延迟 |
|--------|--------|---------|------|
| 1080p  | 6 MB   | 60+     | <5ms |
| 2K     | 10 MB  | 60+     | <8ms |
| 4K     | 24 MB  | 60+     | <12ms|
| 8K     | 95 MB  | 30+     | <20ms|

## 🐛 故障排除

### 问题 1: Native addon 未加载
**错误**: `Failed to load shared memory addon`

**解决**:
```bash
cd native/shared-memory
rm -rf build node_modules
npm install
node-gyp rebuild
```

### 问题 2: 共享内存创建失败
**错误**: `Failed to create shared memory`

**解决 (Linux)**:
```bash
# 检查共享内存限制
cat /proc/sys/kernel/shmmax

# 增加限制 (需要 root)
sudo sysctl -w kernel.shmmax=1073741824  # 1GB
```

### 问题 3: WebGL 不支持
**错误**: `WebGL not supported`

**解决**:
- 更新显卡驱动
- 在 Chrome/Electron 启动参数中启用硬件加速
- 检查 `chrome://gpu` 查看 WebGL 状态

### 问题 4: 帧率低于预期
**可能原因**:
1. CPU 占用过高 → 降低分辨率
2. 共享内存读取慢 → 检查系统负载
3. WebGL 渲染慢 → 更新显卡驱动

## 📝 扩展到真实视频解码

要将此演示扩展为真实的视频解码器:

### 1. 替换图像生成器为 FFmpeg 解码器
```cpp
// 替代 TestImageGenerator
class FFmpegDecoder {
    AVCodecContext* codec_ctx;
    AVBufferRef* hw_device_ctx;  // 硬件加速
    
    // 使用 NVDEC/VAAPI 硬件解码
    void DecodeFrame() {
        // ... FFmpeg 解码逻辑 ...
        // 直接写入共享内存
        memcpy(shm_ptr, frame->data[0], frame_size);
    }
};
```

### 2. 修改为 YUV 格式 (更高效)
- 当前演示使用 RGB (3 bytes/pixel)
- 真实场景使用 YUV420P (1.5 bytes/pixel)
- 节省 50% 内存和带宽

### 3. 添加音频同步
- 使用 Web Audio API
- PTS 时间戳同步

## 🔗 相关资源

- [Node-API 文档](https://nodejs.org/api/n-api.html)
- [POSIX 共享内存](https://man7.org/linux/man-pages/man7/shm_overview.7.html)
- [WebGL 教程](https://developer.mozilla.org/en-US/docs/Web/API/WebGL_API)
- [FFmpeg 硬件加速](https://trac.ffmpeg.org/wiki/HWAccelIntro)

## 📄 许可证

与主项目保持一致
