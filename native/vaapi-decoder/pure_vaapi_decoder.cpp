/**
 * 纯 VA-API H.265 解码器
 * 不依赖 FFmpeg，直接使用 VA-API
 */
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <napi.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <vector>

class PureVaapiDecoder {
private:
    int drm_fd = -1;
    VADisplay va_display = nullptr;
    VAConfigID va_config = VAConfigID();
    VAContextID va_context = VAContextID();
    VASurfaceID* va_surfaces = nullptr;
    int num_surfaces = 0;
    
    bool initialized = false;
    std::string last_error;
    
    // 文件缓冲
    std::vector<uint8_t> file_buffer;
    size_t buffer_pos = 0;
    
    // NV12 输出缓冲
    std::unique_ptr<uint8_t[]> nv12_buffer;
    size_t nv12_buffer_size = 0;
    
    // 视频信息
    int video_width = 6272;   // 从你的视频获取
    int video_height = 3456;
    int current_surface = 0;

    // VA-API 解码缓冲区
    VABufferID pic_param_buf;
    VABufferID slice_param_buf;
    VABufferID slice_data_buf;

    std::vector<VASurfaceID> reference_frames;

  public:
    PureVaapiDecoder() {
      pic_param_buf = VA_INVALID_ID;
      slice_param_buf = VA_INVALID_ID;
      slice_data_buf = VA_INVALID_ID;
    }

    ~PureVaapiDecoder() {
        cleanup();
    }

    void cleanup() {
        if (va_surfaces && num_surfaces > 0) {
            vaDestroySurfaces(va_display, va_surfaces, num_surfaces);
            delete[] va_surfaces;
            va_surfaces = nullptr;
            num_surfaces = 0;
        }
        if (va_context != VAContextID()) {
            vaDestroyContext(va_display, va_context);
            va_context = VAContextID();
        }
        if (va_config != VAConfigID()) {
            vaDestroyConfig(va_display, va_config);
            va_config = VAConfigID();
        }
        if (va_display) {
            vaTerminate(va_display);
            va_display = nullptr;
        }
        if (drm_fd >= 0) {
            close(drm_fd);
            drm_fd = -1;
        }
        file_buffer.clear();
        buffer_pos = 0;
        initialized = false;
    }

    // 初始化 VA-API
    bool initVAAPI() {
        // 打开 DRM 设备
        const char* device_path = "/dev/dri/renderD128";
        drm_fd = open(device_path, O_RDWR);
        if (drm_fd < 0) {
            last_error = "Cannot open DRM device: ";
            last_error += device_path;
            return false;
        }

        // 获取 VA Display
        va_display = vaGetDisplayDRM(drm_fd);
        if (!va_display) {
            last_error = "Cannot get VA display";
            return false;
        }

        // 初始化 VA-API
        int major, minor;
        VAStatus va_status = vaInitialize(va_display, &major, &minor);
        if (va_status != VA_STATUS_SUCCESS) {
            last_error = "Cannot initialize VA-API: ";
            last_error += vaErrorStr(va_status);
            return false;
        }

        fprintf(stderr, "VA-API version: %d.%d\n", major, minor);

        // 创建配置（HEVC Main profile）
        VAProfile profile = VAProfileHEVCMain;
        VAEntrypoint entrypoint = VAEntrypointVLD;
        
        va_status = vaCreateConfig(va_display, profile, entrypoint, nullptr, 0, &va_config);
        if (va_status != VA_STATUS_SUCCESS) {
            last_error = "Cannot create VA config: ";
            last_error += vaErrorStr(va_status);
            return false;
        }

        // 创建解码表面
        num_surfaces = 16; // 缓冲帧数
        va_surfaces = new VASurfaceID[num_surfaces];
        
        va_status = vaCreateSurfaces(va_display, VA_RT_FORMAT_YUV420,
                                     video_width, video_height,
                                     va_surfaces, num_surfaces,
                                     nullptr, 0);
        if (va_status != VA_STATUS_SUCCESS) {
            last_error = "Cannot create surfaces: ";
            last_error += vaErrorStr(va_status);
            return false;
        }

        // 创建解码上下文
        va_status = vaCreateContext(va_display, va_config,
                                    video_width, video_height, VA_PROGRESSIVE,
                                    va_surfaces, num_surfaces,
                                    &va_context);
        if (va_status != VA_STATUS_SUCCESS) {
            last_error = "Cannot create context: ";
            last_error += vaErrorStr(va_status);
            return false;
        }

        fprintf(stderr, "VA-API initialized successfully\n");
        return true;
    }

    // 从文件初始化
    bool initFromFile(const std::string& filename) {
        cleanup();

        if (!initVAAPI()) {
            return false;
        }

        // 读取整个文件到内存
        FILE* fp = fopen(filename.c_str(), "rb");
        if (!fp) {
            last_error = "Cannot open file: " + filename;
            return false;
        }

        fseek(fp, 0, SEEK_END);
        size_t file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        if (file_size == 0) {
            fclose(fp);
            last_error = "Empty file";
            return false;
        }
        
        file_buffer.resize(file_size);
        size_t read_size = fread(file_buffer.data(), 1, file_size, fp);
        fclose(fp);
        
        if (read_size != file_size) {
            last_error = "Failed to read complete file";
            return false;
        }
        
        buffer_pos = 0;
        fprintf(stderr, "Loaded H.265 raw stream: %zu bytes\n", file_size);

        initialized = true;
        return true;
    }

    // 查找下一个 NAL 单元
    bool findNextNAL(size_t& nal_start, size_t& nal_end) {
        if (buffer_pos >= file_buffer.size()) {
            return false;
        }

        // 查找起始码
        nal_start = buffer_pos;
        bool found_start = false;
        
        for (size_t i = buffer_pos; i + 3 < file_buffer.size(); i++) {
            // 检查 0x00 0x00 0x00 0x01 (4字节起始码)
            if (file_buffer[i] == 0 && file_buffer[i+1] == 0 && 
                file_buffer[i+2] == 0 && file_buffer[i+3] == 1) {
                nal_start = i + 4; // 跳过起始码
                buffer_pos = i + 4;
                found_start = true;
                break;
            }
            // 检查 0x00 0x00 0x01 (3字节起始码)
            else if (file_buffer[i] == 0 && file_buffer[i+1] == 0 && file_buffer[i+2] == 1) {
                nal_start = i + 3; // 跳过起始码
                buffer_pos = i + 3;
                found_start = true;
                break;
            }
        }

        if (!found_start) {
            return false;
        }

        // 查找下一个起始码（即当前 NAL 的结束位置）
        for (size_t i = buffer_pos; i + 3 < file_buffer.size(); i++) {
            if ((file_buffer[i] == 0 && file_buffer[i+1] == 0 && 
                 file_buffer[i+2] == 0 && file_buffer[i+3] == 1) ||
                (file_buffer[i] == 0 && file_buffer[i+1] == 0 && file_buffer[i+2] == 1)) {
                nal_end = i;
                return true;
            }
        }

        // 如果没找到下一个起始码，说明是最后一个 NAL
        nal_end = file_buffer.size();
        return true;
    }

    // 解码一帧
    bool decodeFrame(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        if (!initialized) {
            last_error = "Decoder not initialized";
            return false;
        }

        // 1. 查找并收集一帧所需的所有 NAL 单元
        std::vector<std::pair<size_t, size_t>> frame_nals;

        while (true) {
          size_t nal_start, nal_end;
          if (!findNextNAL(nal_start, nal_end)) {
            // 文件结束
            if (frame_nals.empty()) {
              return false;
            }
            break;
          }

          // 检查 NAL 类型
          if (nal_start < file_buffer.size()) {
            uint8_t nal_header = file_buffer[nal_start];
            int nal_type = (nal_header >> 1) & 0x3F;

            frame_nals.push_back({nal_start, nal_end});

            // NAL 类型判断:
            // 0-9: 各种 slice 类型（包含图像数据）
            // 32-34: VPS, SPS, PPS
            if (nal_type >= 0 && nal_type <= 9) {
              // 遇到 slice，假设是一帧
              break;
            }
          }
        }

        if (frame_nals.empty()) {
          return false;
        }

        // 2. 解码这一帧
        if (!decodeFrameInternal(frame_nals)) {
          fprintf(stderr, "Decode failed: %s\n", last_error.c_str());
          return false;
        }

        // 3. 从 VA Surface 获取解码后的数据
        return getSurfaceData(out_data, out_width, out_height, out_size);
    }

  private:
    // 内部解码函数
  bool decodeFrameInternal(
      const std::vector<std::pair<size_t, size_t>> &frame_nals) {
    VAStatus va_status;

    // 准备解码目标 surface
    VASurfaceID target_surface = va_surfaces[current_surface];

    // 开始解码图片
    va_status = vaBeginPicture(va_display, va_context, target_surface);
    if (va_status != VA_STATUS_SUCCESS) {
      last_error = "vaBeginPicture failed: ";
      last_error += vaErrorStr(va_status);
      return false;
    }

    // 提交图片参数
    if (!submitPictureParams()) {
      vaEndPicture(va_display, va_context);
      return false;
    }

    // 遍历所有 NAL 单元，提交参数和数据
    for (const auto &nal : frame_nals) {
      size_t nal_start = nal.first;
      size_t nal_end = nal.second;
      size_t nal_size = nal_end - nal_start;

      if (nal_size == 0)
        continue;

      uint8_t nal_header = file_buffer[nal_start];
      int nal_type = (nal_header >> 1) & 0x3F;

      // 根据 NAL 类型提交不同的参数
      if (nal_type >= 0 && nal_type <= 9) {
        // Slice NAL - 包含图像数据
        if (!submitSliceParams(nal_start, nal_size)) {
          vaEndPicture(va_display, va_context);
          return false;
        }

        if (!submitSliceData(nal_start, nal_size)) {
          vaEndPicture(va_display, va_context);
          return false;
        }
      }
      // VPS/SPS/PPS 也需要作为 slice data 提交
      else if (nal_type >= 32 && nal_type <= 34) {
        if (!submitSliceData(nal_start, nal_size)) {
          vaEndPicture(va_display, va_context);
          return false;
        }
      }
    }

    // 结束解码图片
    va_status = vaEndPicture(va_display, va_context);
    if (va_status != VA_STATUS_SUCCESS) {
      last_error = "vaEndPicture failed: ";
      last_error += vaErrorStr(va_status);
      return false;
    }

    // 同步等待解码完成
    va_status = vaSyncSurface(va_display, target_surface);
    if (va_status != VA_STATUS_SUCCESS) {
      last_error = "vaSyncSurface failed: ";
      last_error += vaErrorStr(va_status);
      return false;
    }

    return true;
  }

    // 提交图片参数
    bool submitPictureParams() {
      VAPictureParameterBufferHEVC pic_param = {};

      // 基本参数设置
      pic_param.pic_width_in_luma_samples = video_width;
      pic_param.pic_height_in_luma_samples = video_height;

      // 重要：设置色度格式
      pic_param.pic_fields.bits.chroma_format_idc = 1; // 1 = 4:2:0
      pic_param.pic_fields.bits.separate_colour_plane_flag = 0;

      // 设置位深度
      pic_param.bit_depth_luma_minus8 = 0;   // 8-bit
      pic_param.bit_depth_chroma_minus8 = 0; // 8-bit

      // 参考帧设置（简化：无参考帧）
      for (int i = 0; i < 15; i++) {
        pic_param.ReferenceFrames[i].picture_id = VA_INVALID_SURFACE;
        pic_param.ReferenceFrames[i].flags = VA_PICTURE_HEVC_INVALID;
      }

      // 当前图片设置
      pic_param.CurrPic.picture_id = va_surfaces[current_surface];
      pic_param.CurrPic.pic_order_cnt = 0;
      pic_param.CurrPic.flags = 0;

      // 打印调试信息
      fprintf(stderr, "Picture params: %dx%d, chroma_format=%d, surface=%d\n",
              pic_param.pic_width_in_luma_samples,
              pic_param.pic_height_in_luma_samples,
              pic_param.pic_fields.bits.chroma_format_idc,
              pic_param.CurrPic.picture_id);

      // 创建参数缓冲
      VAStatus va_status =
          vaCreateBuffer(va_display, va_context, VAPictureParameterBufferType,
                         sizeof(pic_param), 1, &pic_param, &pic_param_buf);
      if (va_status != VA_STATUS_SUCCESS) {
        last_error = "Failed to create picture parameter buffer: ";
        last_error += vaErrorStr(va_status);
        return false;
      }

      // 提交参数
      va_status = vaRenderPicture(va_display, va_context, &pic_param_buf, 1);
      vaDestroyBuffer(va_display, pic_param_buf);
      pic_param_buf = VA_INVALID_ID;

      return va_status == VA_STATUS_SUCCESS;
    }

    // 提交 Slice 参数
    bool submitSliceParams(size_t nal_start, size_t nal_size) {
      VASliceParameterBufferHEVC slice_param = {};

      // 基本参数
      slice_param.slice_data_size = nal_size;
      slice_param.slice_data_offset = 0;
      slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
      slice_param.slice_segment_address = 0;

      // 创建 slice 参数缓冲
      VAStatus va_status = vaCreateBuffer(
          va_display, va_context, VASliceParameterBufferType,
          sizeof(slice_param), 1, &slice_param, &slice_param_buf);
      if (va_status != VA_STATUS_SUCCESS) {
        last_error = "Failed to create slice parameter buffer: ";
        last_error += vaErrorStr(va_status);
        return false;
      }

      // 提交参数
      va_status = vaRenderPicture(va_display, va_context, &slice_param_buf, 1);
      vaDestroyBuffer(va_display, slice_param_buf);
      slice_param_buf = VA_INVALID_ID;

      return va_status == VA_STATUS_SUCCESS;
    }

    // 提交 Slice 数据
    bool submitSliceData(size_t nal_start, size_t nal_size) {
      // 创建 slice 数据缓冲
      VAStatus va_status =
          vaCreateBuffer(va_display, va_context, VASliceDataBufferType,
                         nal_size, 1, &file_buffer[nal_start], &slice_data_buf);
      if (va_status != VA_STATUS_SUCCESS) {
        last_error = "Failed to create slice data buffer: ";
        last_error += vaErrorStr(va_status);
        return false;
      }

      // 提交数据
      va_status = vaRenderPicture(va_display, va_context, &slice_data_buf, 1);
      vaDestroyBuffer(va_display, slice_data_buf);
      slice_data_buf = VA_INVALID_ID;

      return va_status == VA_STATUS_SUCCESS;
    }

    // 从 VA Surface 获取 NV12 数据
    bool getSurfaceData(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
      VASurfaceID surface = va_surfaces[current_surface];

      // 映射表面到系统内存
      VAImage image;
      VAStatus va_status = vaDeriveImage(va_display, surface, &image);
      if (va_status != VA_STATUS_SUCCESS) {
        // 如果不支持 derive，尝试 get image
        VAImageFormat format;
        format.fourcc = VA_FOURCC_NV12;

        va_status = vaCreateImage(va_display, &format, video_width,
                                  video_height, &image);
        if (va_status != VA_STATUS_SUCCESS) {
          last_error = "Cannot create image: ";
          last_error += vaErrorStr(va_status);
          return false;
        }

        va_status = vaGetImage(va_display, surface, 0, 0, video_width,
                               video_height, image.image_id);
        if (va_status != VA_STATUS_SUCCESS) {
          vaDestroyImage(va_display, image.image_id);
          last_error = "Cannot get image: ";
          last_error += vaErrorStr(va_status);
          return false;
        }
      }

        // 打印图像格式信息
        fprintf(
            stderr,
            "Image format: fourcc=0x%x, width=%d, height=%d, num_planes=%d\n",
            image.format.fourcc, image.width, image.height, image.num_planes);
        fprintf(stderr, "  Plane 0: offset=%d, pitch=%d\n", image.offsets[0],
                image.pitches[0]);
        fprintf(stderr, "  Plane 1: offset=%d, pitch=%d\n", image.offsets[1],
                image.pitches[1]);

        // 映射图像数据
        void* image_data = nullptr;
        va_status = vaMapBuffer(va_display, image.buf, &image_data);
        if (va_status != VA_STATUS_SUCCESS) {
            vaDestroyImage(va_display, image.image_id);
            last_error = "Cannot map buffer: ";
            last_error += vaErrorStr(va_status);
            return false;
        }

        // 分配输出缓冲
        int width = image.width;
        int height = image.height;
        size_t nv12_size = width * height * 3 / 2;
        
        if (nv12_buffer_size < nv12_size) {
            nv12_buffer = std::make_unique<uint8_t[]>(nv12_size);
            nv12_buffer_size = nv12_size;
        }

        // 复制 NV12 数据
        uint8_t* src = (uint8_t*)image_data;
        uint8_t* dst = nv12_buffer.get();

        // Y plane - 考虑 pitch 可能不等于 width
        for (int i = 0; i < height; i++) {
            memcpy(dst + i * width, 
                   src + image.offsets[0] + i * image.pitches[0], 
                   width);
        }

        // UV plane - NV12 格式，UV 交错存储
        int uv_height = height / 2;
        for (int i = 0; i < uv_height; i++) {
          memcpy(dst + width * height + i * width,
                 src + image.offsets[1] + i * image.pitches[1],
                 width); // UV 平面的宽度也是 width（包含 U 和 V）
        }

        // 打印前几个像素的值用于调试
        static bool first_frame = true;
        if (first_frame) {
          fprintf(stderr, "First 10 Y values: ");
          for (int i = 0; i < 10; i++) {
            fprintf(stderr, "%d ", dst[i]);
          }
          fprintf(stderr, "\nFirst 10 UV values: ");
          for (int i = 0; i < 10; i++) {
            fprintf(stderr, "%d ", dst[width * height + i]);
          }
          fprintf(stderr, "\n");
          first_frame = false;
        }

        vaUnmapBuffer(va_display, image.buf);
        vaDestroyImage(va_display, image.image_id);

        *out_data = nv12_buffer.get();
        *out_width = width;
        *out_height = height;
        *out_size = nv12_size;

        // 更新 surface 索引（移到这里，因为前面已经用过了）
        current_surface = (current_surface + 1) % num_surfaces;

        return true;
    }

  public:
    bool getVideoInfo(int* width, int* height) {
        if (!initialized) return false;
        *width = video_width;
        *height = video_height;
        return true;
    }

    std::string getLastError() const {
        return last_error;
    }

    void reset() {
        buffer_pos = 0;
        current_surface = 0;
    }
};

// N-API 绑定
class PureVaapiDecoderWrapper : public Napi::ObjectWrap<PureVaapiDecoderWrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "PureVaapiDecoder", {
            InstanceMethod("init", &PureVaapiDecoderWrapper::Init),
            InstanceMethod("decodeFrame", &PureVaapiDecoderWrapper::DecodeFrame),
            InstanceMethod("getVideoInfo", &PureVaapiDecoderWrapper::GetVideoInfo),
            InstanceMethod("getLastError", &PureVaapiDecoderWrapper::GetLastError),
            InstanceMethod("reset", &PureVaapiDecoderWrapper::Reset),
            InstanceMethod("close", &PureVaapiDecoderWrapper::Close),
        });

        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        exports.Set("PureVaapiDecoder", func);
        return exports;
    }

    PureVaapiDecoderWrapper(const Napi::CallbackInfo& info) 
        : Napi::ObjectWrap<PureVaapiDecoderWrapper>(info) {
        decoder_ = std::make_unique<PureVaapiDecoder>();
    }

private:
    std::unique_ptr<PureVaapiDecoder> decoder_;

    Napi::Value Init(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected filename").ThrowAsJavaScriptException();
            return env.Null();
        }
        std::string filename = info[0].As<Napi::String>().Utf8Value();
        bool success = decoder_->initFromFile(filename);
        return Napi::Boolean::New(env, success);
    }

    Napi::Value DecodeFrame(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        uint8_t* data = nullptr;
        int width = 0, height = 0;
        size_t size = 0;

        bool success = decoder_->decodeFrame(&data, &width, &height, &size);
        if (!success) return env.Null();

        Napi::Object result = Napi::Object::New(env);
        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, data, size);
        result.Set("data", buffer);
        result.Set("width", Napi::Number::New(env, width));
        result.Set("height", Napi::Number::New(env, height));
        result.Set("format", Napi::String::New(env, "nv12"));
        return result;
    }

    Napi::Value GetVideoInfo(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        int width = 0, height = 0;
        bool success = decoder_->getVideoInfo(&width, &height);
        if (!success) return env.Null();

        Napi::Object result = Napi::Object::New(env);
        result.Set("width", Napi::Number::New(env, width));
        result.Set("height", Napi::Number::New(env, height));
        return result;
    }

    Napi::Value GetLastError(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        return Napi::String::New(env, decoder_->getLastError());
    }

    Napi::Value Reset(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        decoder_->reset();
        return env.Undefined();
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        decoder_->cleanup();
        return env.Undefined();
    }
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return PureVaapiDecoderWrapper::Init(env, exports);
}

NODE_API_MODULE(pure_vaapi_decoder, Init)