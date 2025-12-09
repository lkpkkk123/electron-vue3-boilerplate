#!/usr/bin/env node

/**
 * 简单的 VA-API 解码器测试脚本
 */

const path = require('path');

const addonPath = path.join(__dirname, 'native/vaapi-decoder/build/Release/simple_vaapi_decoder.node');

console.log('Loading decoder from:', addonPath);

try {
  const { SimpleVaapiDecoder } = require(addonPath);
  console.log('✓ Decoder loaded successfully\n');
  
  const decoder = new SimpleVaapiDecoder();
  
  // 测试文件
  const videoFile = '/home/likp/Public/osd2.265';
  const codec = 'hevc'; // 或 'h264'
  
  console.log('Initializing decoder...');
  console.log('  File:', videoFile);
  console.log('  Codec:', codec);
  
  const success = decoder.init(videoFile, codec);
  
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
  for (let i = 0; i < 10; i++) {
    const frame = decoder.decodeFrame();
    
    if (!frame) {
      console.log(`Frame ${i + 1}: End of stream`);
      break;
    }
    
    console.log(`Frame ${i + 1}:`);
    console.log(`  Size: ${frame.width} x ${frame.height}`);
    console.log(`  Format: ${frame.format}`);
    console.log(`  Data size: ${frame.data.length} bytes`);
    
    // 验证数据大小
    const expectedSize = frame.width * frame.height * 3 / 2; // NV12 格式
    if (frame.data.length === expectedSize) {
      console.log(`  ✓ Data size correct`);
    } else {
      console.log(`  ✗ Data size mismatch (expected ${expectedSize})`);
    }
    console.log('');
  }
  
  // 测试重置
  console.log('Testing reset...');
  decoder.reset();
  const firstFrame = decoder.decodeFrame();
  if (firstFrame) {
    console.log('✓ Reset successful, decoded first frame again');
    console.log(`  Size: ${firstFrame.width} x ${firstFrame.height}\n`);
  } else {
    console.log('✗ Reset failed\n');
  }
  
  // 清理
  decoder.close();
  console.log('✓ Test completed successfully');
  
} catch (error) {
  console.error('✗ Error:', error.message);
  console.error(error.stack);
  process.exit(1);
}
