#!/bin/bash
# VA-API 视频解码器编译脚本

set -e

echo "=== Building VA-API Video Decoder ==="

# 检查依赖
echo "Checking dependencies..."

if ! pkg-config --exists libva; then
    echo "Error: libva not found. Please install:"
    echo "  Ubuntu/Debian: sudo apt-get install libva-dev libva-drm2"
    echo "  Fedora: sudo dnf install libva-devel"
    echo "  Arch: sudo pacman -S libva"
    exit 1
fi

if ! pkg-config --exists libavcodec; then
    echo "Error: FFmpeg libavcodec not found. Please install:"
    echo "  Ubuntu/Debian: sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev"
    echo "  Fedora: sudo dnf install ffmpeg-devel"
    echo "  Arch: sudo pacman -S ffmpeg"
    exit 1
fi

# 切换到 native addon 目录
cd "$(dirname "$0")/../native/vaapi-decoder"

# 安装依赖
echo "Installing npm dependencies..."
npm install

# 编译
echo "Building native addon..."
npm run build

echo "=== Build completed successfully ==="
echo "Output: $(pwd)/build/Release/vaapi_decoder.node"

# 验证
if [ -f "build/Release/vaapi_decoder.node" ]; then
    SIZE=$(du -h build/Release/vaapi_decoder.node | cut -f1)
    echo "Binary size: $SIZE"
else
    echo "Error: Build output not found!"
    exit 1
fi
