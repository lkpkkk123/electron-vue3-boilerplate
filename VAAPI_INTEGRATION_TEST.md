# VA-API 视频解码集成测试

## 已完成的集成

已将 VA-API 解码器集成到 test-video-player 中，现在可以直接从真实视频文件解码并渲染。

### 修改的文件

1. **test-video-preload.ts**
   - 加载 VA-API 解码器 addon
   - 实现 `getImageFromVideo()` 函数
   - 自动初始化解码器并循环播放视频
   - 在 stop 时清理解码器资源

2. **test-video-player.vue**
   - 添加 `getImageFromVideo` 类型声明
   - 修改 `rendFrame()` 调用解码器而非生成测试图案

### 工作流程

```
启动播放
    ↓
getImageFromVideo(width, height)
    ↓
检查解码器是否初始化
    ├─ 否 → 创建解码器并打开视频文件
    └─ 是 → 继续
    ↓
解码一帧
    ├─ 成功 → 返回 NV12 Buffer
    └─ 到达结尾 → 重新初始化（循环播放）
    ↓
WebGLNV12Renderer 渲染
    ↓
显示到 Canvas
```

### 使用方法

1. **确保视频文件存在**
   ```bash
   # 确认文件路径
   ls -lh /home/likp/Public/osd2.mp4
   ```

2. **构建项目**
   ```bash
   cd /home/likp/work/electron-vue3-boilerplate
   yarn run build
   ```

3. **启动开发模式**
   ```bash
   yarn run dev
   ```

4. **在应用中测试**
   - 打开测试视频窗口
   - 点击"开始"按钮
   - 视频应该自动解码并播放
   - 到达末尾后自动循环

### 视频信息

当前使用的视频: `/home/likp/Public/osd2.mp4`

初始化时会在控制台输出:
```
[Preload] Video initialized: 1920x1080 @ 30.00 fps, codec: h264
```

每帧解码时输出:
```
[Preload] Decoded frame: 1920x1080, 3.04 MB
```

### 性能说明

- **格式**: NV12 (YUV 420)
- **大小**: 1.5 字节/像素
- **1080p**: ~3.04 MB/帧
- **解码**: 硬件加速 (VA-API)
- **渲染**: WebGL YUV→RGB 转换

### 修改视频文件

如需使用其他视频文件，修改 `test-video-preload.ts`:

```typescript
const videoPath = "/path/to/your/video.mp4";  // 改成你的视频路径
```

支持的格式:
- H264 (.mp4, .h264, .mkv)
- H265/HEVC (.mp4, .hevc, .mkv)

### 调试

开启详细日志:
```bash
# 设置环境变量
export LIBVA_MESSAGING_LEVEL=2

# 运行应用
yarn run dev
```

查看解码器状态:
```bash
# 检查 VA-API 可用性
vainfo

# 查看支持的编码格式
vainfo | grep VAProfile
```

### 已知问题

1. **视频分辨率不匹配**: 
   - 如果视频分辨率与选择的分辨率不同，会自动使用视频的实际分辨率
   - 解决: 使用视频的原始分辨率或在界面上禁用分辨率选择

2. **循环播放延迟**:
   - 重新初始化解码器可能有轻微延迟
   - 解决: 可以实现 seek 到开头而非重新初始化

3. **内存使用**:
   - 长时间播放可能积累内存
   - 解决: 定期重启或实现更好的资源管理

### 后续优化

1. **动态视频路径**: 添加文件选择对话框
2. **播放控制**: 暂停、快进、后退
3. **多视频支持**: 切换不同视频文件
4. **性能监控**: 添加解码时间统计
5. **错误处理**: 更完善的异常处理和用户提示

## 测试检查清单

- [ ] 编译成功 (`yarn run build`)
- [ ] 应用启动正常 (`yarn run dev`)
- [ ] 解码器初始化成功（查看控制台）
- [ ] 视频正常播放
- [ ] 帧率稳定（30 fps）
- [ ] 循环播放正常
- [ ] 停止时资源释放（无内存泄漏）
- [ ] 性能指标正常（解码耗时 < 10ms）
