/**
 * VA-API 解码器测试脚本
 */

const path = require('path');

// 加载解码器
try {
    const addon = require('./native/vaapi-decoder/build/Release/vaapi_decoder.node');
    console.log('✓ VA-API decoder addon loaded successfully');
    
    const decoder = new addon.VaapiDecoder();
    console.log('✓ VaapiDecoder instance created');
    
    // 测试初始化（需要实际的视频文件）
    const testFile = process.argv[2];
    
    if (testFile) {
        console.log(`\nTesting with file: ${testFile}`);
        
        const success = decoder.initFromFile(testFile);
        
        if (success) {
            console.log('✓ Decoder initialized');
            
            const info = decoder.getVideoInfo();
            if (info) {
                console.log('\nVideo Information:');
                console.log(`  Resolution: ${info.width}x${info.height}`);
                console.log(`  Codec: ${info.codec}`);
                console.log(`  FPS: ${info.fps.toFixed(2)}`);
                
                // 解码几帧测试
                console.log('\nDecoding frames...');
                for (let i = 0; i < 20; i++) {
                    const frame = decoder.decodeFrame();
                    if (frame) {
                        const sizeMB = (frame.data.length / 1024 / 1024).toFixed(2);
                        console.log(`  Frame ${i + 1}: ${frame.width}x${frame.height}, ${sizeMB} MB, format: ${frame.format}`);
                    } else {
                        console.log('  No more frames');
                        break;
                    }
                }
                
                decoder.close();
                console.log('\n✓ Decoder closed');
            }
        } else {
            console.error('✗ Failed to initialize decoder');
            console.error('  Make sure the file exists and is a valid H264/H265 video');
            console.error('  Also check that VA-API is available (run "vainfo" to verify)');
        }
    } else {
        console.log('\nUsage: node test-decoder.js <video-file.h264|mp4|mkv>');
        console.log('Example: node test-decoder.js /path/to/video.mp4');
    }
    
} catch (err) {
    console.error('✗ Error:', err.message);
    console.error('\nPossible causes:');
    console.error('  1. Native addon not built - run: ./scripts/build-vaapi-decoder.sh');
    console.error('  2. Missing dependencies - install libva and FFmpeg');
    console.error('  3. VA-API not available - check with: vainfo');
}
