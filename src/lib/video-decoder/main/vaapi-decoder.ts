/**
 * VA-API 视频解码器 TypeScript 绑定
 */

export interface DecodedFrame {
  data: Buffer;      // NV12 格式的帧数据
  width: number;     // 宽度
  height: number;    // 高度
  format: 'nv12';    // 像素格式
}

export interface VideoInfo {
  width: number;     // 视频宽度
  height: number;    // 视频高度
  codec: string;     // 编码格式 (h264/hevc)
  fps: number;       // 帧率
}

export class VaapiDecoder {
  private decoder: any;

  constructor() {
    try {
      // 尝试加载编译好的 native addon
      const addon = require('../../../native/vaapi-decoder/build/Release/vaapi_decoder.node');
      this.decoder = new addon.VaapiDecoder();
    } catch (err) {
      throw new Error(`Failed to load VA-API decoder: ${err}`);
    }
  }

  /**
   * 从文件初始化解码器
   * @param filename 视频文件路径
   * @returns 是否成功
   */
  initFromFile(filename: string): boolean {
    return this.decoder.initFromFile(filename);
  }

  /**
   * 从缓冲区初始化解码器
   * @param buffer 视频数据
   * @param codecName 编码器名称 (h264_vaapi, hevc_vaapi)
   * @returns 是否成功
   */
  initFromBuffer(buffer: Buffer, codecName: string): boolean {
    return this.decoder.initFromBuffer(buffer, codecName);
  }

  /**
   * 解码一帧（从文件）
   * @returns 解码后的帧数据，如果到达文件末尾返回 null
   */
  decodeFrame(): DecodedFrame | null {
    return this.decoder.decodeFrame();
  }

  /**
   * 解码数据包（从内存）
   * @param packet 编码的数据包
   * @returns 解码后的帧数据，如果需要更多数据返回 null
   */
  decodePacket(packet: Buffer): DecodedFrame | null {
    return this.decoder.decodePacket(packet);
  }

  /**
   * 获取视频信息
   * @returns 视频信息
   */
  getVideoInfo(): VideoInfo | null {
    return this.decoder.getVideoInfo();
  }

  /**
   * 关闭解码器，释放资源
   */
  close(): void {
    this.decoder.close();
  }
}

/**
 * 辅助函数：解码整个视频文件
 */
export async function decodeVideoFile(
  filename: string,
  onFrame: (frame: DecodedFrame, frameNumber: number) => void | Promise<void>
): Promise<void> {
  const decoder = new VaapiDecoder();

  if (!decoder.initFromFile(filename)) {
    throw new Error('Failed to initialize decoder');
  }

  const info = decoder.getVideoInfo();
  console.log('Video info:', info);

  let frameNumber = 0;
  let frame: DecodedFrame | null;

  while ((frame = decoder.decodeFrame()) !== null) {
    await onFrame(frame, frameNumber++);
  }

  decoder.close();
}

/**
 * 辅助函数：解码 H264/H265 数据流
 */
export class StreamDecoder {
  private decoder: VaapiDecoder;
  private frameCallback: (frame: DecodedFrame, frameNumber: number) => void;
  private frameNumber = 0;

  constructor(
    codecName: 'h264_vaapi' | 'hevc_vaapi',
    onFrame: (frame: DecodedFrame, frameNumber: number) => void
  ) {
    this.decoder = new VaapiDecoder();
    this.frameCallback = onFrame;

    // 使用空 buffer 初始化，只设置编码器类型
    const dummyBuffer = Buffer.alloc(0);
    if (!this.decoder.initFromBuffer(dummyBuffer, codecName)) {
      throw new Error('Failed to initialize stream decoder');
    }
  }

  /**
   * 推送编码数据包进行解码
   */
  push(packet: Buffer): void {
    const frame = this.decoder.decodePacket(packet);
    if (frame) {
      this.frameCallback(frame, this.frameNumber++);
    }
  }

  /**
   * 关闭解码器
   */
  close(): void {
    this.decoder.close();
  }
}
