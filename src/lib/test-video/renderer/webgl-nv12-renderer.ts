/**
 * WebGL NV12 YUV 图像渲染器
 * 从共享内存读取 NV12 数据并渲染到 Canvas
 * NV12 格式: Y平面 (width * height) + 交错UV平面 (width * height / 2)
 */

export class WebGLNV12Renderer {
  private gl: WebGLRenderingContext;
  private program: WebGLProgram;
  private yTexture: WebGLTexture | null;
  private uvTexture: WebGLTexture | null;
  private width: number = 0;
  private height: number = 0;

  // 顶点着色器
  private readonly vertexShaderSource = `
    attribute vec2 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    
    void main() {
      gl_Position = vec4(a_position, 0.0, 1.0);
      v_texCoord = a_texCoord;
    }
  `;

  // 片元着色器 - NV12 to RGB 转换
  private readonly fragmentShaderSource = `
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_textureY;
    uniform sampler2D u_textureUV;
    
    void main() {
      // 采样 Y 值
      float y = texture2D(u_textureY, v_texCoord).r;
      
      // 采样 UV 值 (NV12 格式: U 和 V 交错存储)
      vec2 uv = texture2D(u_textureUV, v_texCoord).ra;
      
      // YUV to RGB 转换 (BT.601 标准)
      // Y 范围: 16-235, UV 范围: 16-240
      y = (y - 0.0625) * 1.164;
      float u = uv.x - 0.5;
      float v = uv.y - 0.5;
      
      float r = y + 1.596 * v;
      float g = y - 0.391 * u - 0.813 * v;
      float b = y + 2.018 * u;
      
      gl_FragColor = vec4(r, g, b, 1.0);
    }
  `;

  constructor(canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl", {
      alpha: false,
      antialias: false,
      preserveDrawingBuffer: false,
    });
    
    if (!gl) {
      throw new Error("WebGL not supported");
    }
    
    this.gl = gl;
    this.program = this.createProgram();
    this.yTexture = this.createTexture();
    this.uvTexture = this.createTexture();
    this.setupGeometry();

    console.log("WebGL NV12 Renderer initialized");
  }

  private createProgram(): WebGLProgram {
    const gl = this.gl;
    
    const vertexShader = this.compileShader(gl.VERTEX_SHADER, this.vertexShaderSource);
    const fragmentShader = this.compileShader(gl.FRAGMENT_SHADER, this.fragmentShaderSource);
    
    const program = gl.createProgram()!;
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      const info = gl.getProgramInfoLog(program);
      throw new Error("Program link failed: " + info);
    }
    
    gl.useProgram(program);
    return program;
  }

  private compileShader(type: number, source: string): WebGLShader {
    const gl = this.gl;
    const shader = gl.createShader(type)!;
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      const info = gl.getShaderInfoLog(shader);
      gl.deleteShader(shader);
      throw new Error("Shader compile failed: " + info);
    }
    
    return shader;
  }

  private createTexture(): WebGLTexture {
    const gl = this.gl;
    const texture = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, texture);
    
    // 纹理参数 - 使用 LINEAR 以获得更好的色度采样质量
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    
    return texture;
  }

  private setupGeometry(): void {
    const gl = this.gl;
    
    // 全屏四边形顶点 (两个三角形)
    const positions = new Float32Array([
      -1, -1,  // 左下
       1, -1,  // 右下
      -1,  1,  // 左上
      -1,  1,  // 左上
       1, -1,  // 右下
       1,  1,  // 右上
    ]);
    
    // 纹理坐标 (翻转Y轴，因为纹理坐标原点在左下)
    const texCoords = new Float32Array([
      0, 1,  // 左下
      1, 1,  // 右下
      0, 0,  // 左上
      0, 0,  // 左上
      1, 1,  // 右下
      1, 0,  // 右上
    ]);
    
    // 位置缓冲
    const positionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, positions, gl.STATIC_DRAW);
    
    const positionLocation = gl.getAttribLocation(this.program, "a_position");
    gl.enableVertexAttribArray(positionLocation);
    gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
    
    // 纹理坐标缓冲
    const texCoordBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, texCoordBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, texCoords, gl.STATIC_DRAW);
    
    const texCoordLocation = gl.getAttribLocation(this.program, "a_texCoord");
    gl.enableVertexAttribArray(texCoordLocation);
    gl.vertexAttribPointer(texCoordLocation, 2, gl.FLOAT, false, 0, 0);
  }

  /**
   * 渲染 NV12 图像
   * @param nv12Data NV12 数据 (Y平面: width*height, UV平面: width*height/2)
   * @param width 图像宽度
   * @param height 图像高度
   */
  public renderFrame(nv12Data: Uint8Array, width: number, height: number): void {
    const gl = this.gl;
    
    // 更新 canvas 尺寸 (如果需要)
    if (this.width !== width || this.height !== height) {
      this.width = width;
      this.height = height;
      
      const canvas = gl.canvas as HTMLCanvasElement;
      canvas.width = width;
      canvas.height = height;
      gl.viewport(0, 0, width, height);
      
      console.log(`Canvas resized to ${width}x${height}`);
    }
    
    // 分离 Y 和 UV 平面
    const ySize = width * height;
    const uvSize = width * height / 2;
    const yData = nv12Data.subarray(0, ySize);
    const uvData = nv12Data.subarray(ySize, ySize + uvSize);
    
    // 上传 Y 纹理 (LUMINANCE 单通道)
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.yTexture);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,                   // mipmap level
      gl.LUMINANCE,        // internal format
      width,
      height,
      0,                   // border
      gl.LUMINANCE,        // format
      gl.UNSIGNED_BYTE,    // type
      yData                // data
    );
    
    // 上传 UV 纹理 (LUMINANCE_ALPHA 双通道交错)
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.uvTexture);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,                      // mipmap level
      gl.LUMINANCE_ALPHA,     // internal format (U 和 V 交错)
      width / 2,              // UV 宽度是 Y 的一半
      height / 2,             // UV 高度是 Y 的一半
      0,                      // border
      gl.LUMINANCE_ALPHA,     // format
      gl.UNSIGNED_BYTE,       // type
      uvData                  // data
    );
    
    // 绑定纹理到着色器
    const yTextureLocation = gl.getUniformLocation(this.program, "u_textureY");
    const uvTextureLocation = gl.getUniformLocation(this.program, "u_textureUV");
    gl.uniform1i(yTextureLocation, 0);  // TEXTURE0
    gl.uniform1i(uvTextureLocation, 1); // TEXTURE1
    
    // 绘制
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  /**
   * 清空画布
   */
  public clear(): void {
    const gl = this.gl;
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
  }

  /**
   * 销毁资源
   */
  public dispose(): void {
    const gl = this.gl;
    if (this.yTexture) {
      gl.deleteTexture(this.yTexture);
      this.yTexture = null;
    }
    if (this.uvTexture) {
      gl.deleteTexture(this.uvTexture);
      this.uvTexture = null;
    }
    if (this.program) {
      gl.deleteProgram(this.program);
    }
  }
}
