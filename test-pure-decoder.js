#!/usr/bin/env node

/**
 * 纯 VA-API 解码器测试脚本（不依赖 FFmpeg）
 */

const path = require('path');
const fs = require('fs');
const addonPath = path.join(__dirname, 'native/vaapi-decoder/build/Release/pure_vaapi_decoder.node');

console.log('Loading pure VA-API decoder from:', addonPath);

try {
  const { PureVaapiDecoder } = require(addonPath);
  console.log('✓ Pure VA-API decoder loaded successfully\n');
  
  const decoder = new PureVaapiDecoder();
  
  // 测试文件
  const videoFile = '/home/likp/Public/osd2.265';
  
  console.log('Initializing decoder...');
  console.log('  File:', videoFile);
  
  const success = decoder.init(videoFile);
  
  if (!success) {
    const error = decoder.getLastError();
    console.error('✗ Failed to initialize decoder');
    console.error('  Error:', error);
    process.exit(1);
  }
  
  console.log('✓ Decoder initialized\n');
  
  // 获取视频信息
  const info = decoder.getVideoInfo();
  if (info) {
    console.log('Video information:');
    console.log('  Resolution:', info.width, 'x', info.height);
    console.log('');
  }
  
  // 解码几帧测试
  console.log('Decoding frames...\n');
  for (let i = 0; i < 5; i++) {
    const frame = decoder.decodeFrame();
    
    if (!frame) {
      console.log(`Frame ${i + 1}: No frame available`);
      const error = decoder.getLastError();
      if (error) {
        console.log(`  Error: ${error}`);
      }
      continue;
    }
    
    console.log(`Frame ${i + 1}:`);
    console.log(`  Size: ${frame.width} x ${frame.height}`);
    console.log(`  Format: ${frame.format}`);
    console.log(`  Data size: ${frame.data.length} bytes`);
    
    // 验证数据大小
    const expectedSize = frame.width * frame.height * 3 / 2; // NV12 格式
    //将frame写到～/tmp/frame_<i + 1>.yuv文件

    const outputPath = `/tmp/frame_${i + 1}.yuv`;
    fs.writeFileSync(outputPath, frame.data);
    if (frame.data.length === expectedSize) {
      console.log(`  ✓ Data size correct`);
    } else {
      console.log(`  ⚠ Data size: ${frame.data.length}, expected: ${expectedSize}`);
    }
    console.log('');
  }
  
  // 清理
  decoder.close();
  console.log('✓ Test completed');
  
} catch (error) {
  console.error('✗ Error:', error.message);
  console.error(error.stack);
  process.exit(1);
}
