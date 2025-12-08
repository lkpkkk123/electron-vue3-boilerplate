#!/bin/bash

# 共享内存 Native Addon 编译脚本

set -e

echo "========================================="
echo "编译共享内存 Native Addon"
echo "========================================="

cd "$(dirname "$0")/../native/shared-memory"

# 检查 Electron 版本
ELECTRON_VERSION=$(node -p "require('../../package.json').devDependencies.electron.replace('^', '')")
echo "Electron 版本: $ELECTRON_VERSION"

# 安装依赖
echo ""
echo "安装依赖..."
npm install

# 编译
echo ""
echo "编译 Native Addon..."
npx node-gyp rebuild \
  --target=$ELECTRON_VERSION \
  --arch=x64 \
  --dist-url=https://electronjs.org/headers

# 检查编译结果
if [ -f "build/Release/shared_memory.node" ]; then
  echo ""
  echo "✅ 编译成功！"
  echo "输出文件: build/Release/shared_memory.node"
  ls -lh build/Release/shared_memory.node
else
  echo ""
  echo "❌ 编译失败！"
  exit 1
fi

echo ""
echo "========================================="
echo "完成！现在可以运行 'yarn run dev' 启动应用"
echo "========================================="
