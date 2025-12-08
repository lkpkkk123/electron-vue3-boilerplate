/**
 * WebGL RGB 图像渲染器
 * 从共享内存读取 RGB 数据并渲染到 Canvas
 */

export class WebGLRGBRenderer {
  private gl: WebGLRenderingContext;
  private program: WebGLProgram;
  private texture: WebGLTexture | null;
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

  // 片元着色器 - RGB 纹理直接渲染
  private readonly fragmentShaderSource = `
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    
    void main() {
      gl_FragColor = texture2D(u_texture, v_texCoord);
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
    this.texture = this.createTexture();
    this.setupGeometry();

    console.log("WebGL Renderer initialized");
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
    
    // 纹理参数 - 使用 NEAREST 以获得最佳性能
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    
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
   * 渲染 RGB 图像
   * @param rgbData RGB 数据 (宽 x 高 x 3)
   * @param width 图像宽度
   * @param height 图像高度
   */
  public renderFrame(rgbData: Uint8Array, width: number, height: number): void {
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
    
    // 上传纹理数据
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,                // mipmap level
      gl.RGB,           // internal format
      width,
      height,
      0,                // border
      gl.RGB,           // format
      gl.UNSIGNED_BYTE, // type
      rgbData           // data
    );
    
    // 绑定纹理到纹理单元 0
    const textureLocation = gl.getUniformLocation(this.program, "u_texture");
    gl.uniform1i(textureLocation, 0);
    
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
    if (this.texture) {
      gl.deleteTexture(this.texture);
      this.texture = null;
    }
    if (this.program) {
      gl.deleteProgram(this.program);
    }
  }
}
