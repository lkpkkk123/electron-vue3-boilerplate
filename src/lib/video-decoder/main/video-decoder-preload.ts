/**
 * Video Decoder Preload Script
 * 暴露 VA-API 解码器 API 到渲染进程
 */

import { ipcRenderer } from 'electron';
import { VaapiDecoder, DecodedFrame, VideoInfo } from './vaapi-decoder';

// 暴露到渲染进程的 API
(window as any).videoDecoderAPI = {
  /**
   * 从文件初始化解码器
   */
  initFromFile: async (filename: string): Promise<boolean> => {
    try {
      const decoder = new VaapiDecoder();
      const result = decoder.initFromFile(filename);
      // 将解码器实例保存到全局
      (global as any).__currentDecoder = decoder;
      return result;
    } catch (err) {
      console.error('initFromFile error:', err);
      return false;
    }
  },

  /**
   * 解码一帧
   */
  decodeFrame: async (): Promise<DecodedFrame | null> => {
    try {
      const decoder = (global as any).__currentDecoder as VaapiDecoder;
      if (!decoder) {
        throw new Error('Decoder not initialized');
      }
      return decoder.decodeFrame();
    } catch (err) {
      console.error('decodeFrame error:', err);
      return null;
    }
  },

  /**
   * 获取视频信息
   */
  getVideoInfo: async (): Promise<VideoInfo | null> => {
    try {
      const decoder = (global as any).__currentDecoder as VaapiDecoder;
      if (!decoder) {
        throw new Error('Decoder not initialized');
      }
      return decoder.getVideoInfo();
    } catch (err) {
      console.error('getVideoInfo error:', err);
      return null;
    }
  },

  /**
   * 关闭解码器
   */
  close: (): void => {
    try {
      const decoder = (global as any).__currentDecoder as VaapiDecoder;
      if (decoder) {
        decoder.close();
        (global as any).__currentDecoder = null;
      }
    } catch (err) {
      console.error('close error:', err);
    }
  },

  /**
   * 从内存初始化解码器
   */
  initFromBuffer: async (buffer: Buffer, codecName: string): Promise<boolean> => {
    try {
      const decoder = new VaapiDecoder();
      const result = decoder.initFromBuffer(buffer, codecName);
      (global as any).__currentDecoder = decoder;
      return result;
    } catch (err) {
      console.error('initFromBuffer error:', err);
      return false;
    }
  },

  /**
   * 解码数据包
   */
  decodePacket: async (packet: Buffer): Promise<DecodedFrame | null> => {
    try {
      const decoder = (global as any).__currentDecoder as VaapiDecoder;
      if (!decoder) {
        throw new Error('Decoder not initialized');
      }
      return decoder.decodePacket(packet);
    } catch (err) {
      console.error('decodePacket error:', err);
      return null;
    }
  },
};

console.log('Video Decoder API initialized');
