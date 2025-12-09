/**
 * VA-API Hardware Video Decoder
 * 支持 H264/H265 硬件解码，输出 NV12 格式
 */
#include <napi.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixdesc.h>
}

#include <memory>
#include <string>
#include <cstring>

class VaapiDecoder {
private:
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw_frame = nullptr;
    AVPacket* packet = nullptr;
    
    int video_stream_idx = -1;
    int drm_fd = -1;
    bool initialized = false;
    bool use_hw_accel = true;  // 是否使用硬件加速
    std::string last_error;     // 最后的错误信息
    
    // NV12 输出缓冲
    std::unique_ptr<uint8_t[]> nv12_buffer;
    size_t nv12_buffer_size = 0;

public:
    VaapiDecoder() {
        frame = av_frame_alloc();
        sw_frame = av_frame_alloc();
        packet = av_packet_alloc();
    }

    ~VaapiDecoder() {
        cleanup();
        if (frame) av_frame_free(&frame);
        if (sw_frame) av_frame_free(&sw_frame);
        if (packet) av_packet_free(&packet);
    }

    void cleanup() {
        if (codec_ctx) {
            avcodec_free_context(&codec_ctx);
            codec_ctx = nullptr;
        }
        if (fmt_ctx) {
            avformat_close_input(&fmt_ctx);
            fmt_ctx = nullptr;
        }
        if (hw_device_ctx) {
            av_buffer_unref(&hw_device_ctx);
            hw_device_ctx = nullptr;
        }
        if (drm_fd >= 0) {
            close(drm_fd);
            drm_fd = -1;
        }
        initialized = false;
    }

    // 初始化 VA-API 设备
    bool initVAAPI(const std::string& device_path = "/dev/dri/renderD128") {
        // 打开 DRM 设备
        drm_fd = open(device_path.c_str(), O_RDWR);
        if (drm_fd < 0) {
            last_error = "Failed to open DRM device: " + device_path + " (errno: " + std::to_string(errno) + ")";
            return false;
        }

        // 创建 VA-API 硬件设备上下文
        int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                         device_path.c_str(), nullptr, 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            last_error = "Failed to create VA-API device context: " + std::string(errbuf);
            close(drm_fd);
            drm_fd = -1;
            return false;
        }

        return true;
    }

    // 初始化解码器（从文件）
    bool initFromFile(const std::string& filename) {
        cleanup();

        // 尝试初始化 VA-API
        use_hw_accel = initVAAPI();
        if (!use_hw_accel) {
            // VA-API 初始化失败，使用软件解码
            fprintf(stderr, "VA-API initialization failed: %s\n", last_error.c_str());
            fprintf(stderr, "Falling back to software decoding...\n");
        }

        // 检查文件是否存在
        if (access(filename.c_str(), F_OK) != 0) {
            last_error = "File does not exist: " + filename;
            fprintf(stderr, "Error: %s\n", last_error.c_str());
            return false;
        }
        
        if (access(filename.c_str(), R_OK) != 0) {
            last_error = "File not readable (permission denied): " + filename;
            fprintf(stderr, "Error: %s\n", last_error.c_str());
            return false;
        }

        // 打开输入文件 - 使用 nullptr options 来使用默认协议
        AVDictionary* options = nullptr;
        int ret = avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, &options);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            last_error = "Failed to open file: " + filename + " - " + std::string(errbuf);
            fprintf(stderr, "Error opening file: %s (ret=%d)\n", last_error.c_str(), ret);
            fprintf(stderr, "FFmpeg error: %s\n", errbuf);
            
            // 列出可用的协议
            void* opaque = nullptr;
            const char* protocol_name;
            fprintf(stderr, "Available input protocols: ");
            while ((protocol_name = avio_enum_protocols(&opaque, 0)) != nullptr) {
                fprintf(stderr, "%s ", protocol_name);
            }
            fprintf(stderr, "\n");
            
            if (options) {
                av_dict_free(&options);
            }
            return false;
        }
        
        if (options) {
            av_dict_free(&options);
        }

        // 查找流信息
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
            last_error = "Failed to find stream info";
            cleanup();
            return false;
        }

        // 查找视频流
        const AVCodec* decoder = nullptr;
        video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (video_stream_idx < 0) {
            last_error = "No video stream found";
            cleanup();
            return false;
        }

        // 创建解码器上下文
        codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            last_error = "Failed to allocate codec context";
            cleanup();
            return false;
        }

        // 复制流参数到解码器上下文
        if (avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_idx]->codecpar) < 0) {
            last_error = "Failed to copy codec parameters";
            cleanup();
            return false;
        }

        // 如果使用硬件加速，设置硬件设备上下文
        if (use_hw_accel && hw_device_ctx) {
            codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
            codec_ctx->get_format = get_hw_format;
        }

        // 打开解码器
        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            last_error = "Failed to open decoder";
            cleanup();
            return false;
        }

        initialized = true;
        fprintf(stderr, "Decoder initialized successfully (HW accel: %s)\n", use_hw_accel ? "YES" : "NO");
        return true;
    }

    // 初始化解码器（从内存数据）
    bool initFromBuffer(const uint8_t* data, size_t size, const std::string& codec_name) {
        cleanup();

        // 初始化 VA-API
        if (!initVAAPI()) {
            return false;
        }

        // 查找解码器
        const AVCodec* decoder = avcodec_find_decoder_by_name(codec_name.c_str());
        if (!decoder) {
            cleanup();
            return false;
        }

        // 创建解码器上下文
        codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            cleanup();
            return false;
        }

        // 设置硬件加速
        codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        codec_ctx->get_format = get_hw_format;

        // 打开解码器
        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            cleanup();
            return false;
        }

        initialized = true;
        return true;
    }

    // 解码一帧（从文件）
    bool decodeFrame(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        if (!initialized) return false;

        while (true) {
            // 读取数据包
            int ret = av_read_frame(fmt_ctx, packet);
            if (ret < 0) {
                // 文件结束或错误
                return false;
            }

            if (packet->stream_index != video_stream_idx) {
                av_packet_unref(packet);
                continue;
            }

            // 发送数据包到解码器
            ret = avcodec_send_packet(codec_ctx, packet);
            av_packet_unref(packet);

            if (ret < 0) {
                return false;
            }

            // 接收解码后的帧
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN)) {
                continue;
            } else if (ret < 0) {
                return false;
            }

            // 成功解码一帧
            return extractNV12Frame(out_data, out_width, out_height, out_size);
        }
    }

    // 解码数据包（从内存）
    bool decodePacket(const uint8_t* packet_data, size_t packet_size,
                     uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        if (!initialized) return false;

        // 设置数据包
        packet->data = const_cast<uint8_t*>(packet_data);
        packet->size = packet_size;

        // 发送数据包到解码器
        int ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            return false;
        }

        // 接收解码后的帧
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN)) {
            // 需要更多数据
            return false;
        } else if (ret < 0) {
            return false;
        }

        // 成功解码一帧
        return extractNV12Frame(out_data, out_width, out_height, out_size);
    }

    // 获取视频信息
    bool getVideoInfo(int* width, int* height, std::string* codec_name, int* fps_num, int* fps_den) {
        if (!initialized || !fmt_ctx) return false;

        AVStream* stream = fmt_ctx->streams[video_stream_idx];
        *width = codec_ctx->width;
        *height = codec_ctx->height;
        *codec_name = avcodec_get_name(codec_ctx->codec_id);
        *fps_num = stream->avg_frame_rate.num;
        *fps_den = stream->avg_frame_rate.den;

        return true;
    }

    // 获取最后的错误信息
    std::string getLastError() const {
        return last_error;
    }

private:
    // 获取硬件像素格式
    static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
        for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == AV_PIX_FMT_VAAPI) {
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    }

    // 提取 NV12 格式数据
    bool extractNV12Frame(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        AVFrame* target_frame = frame;

        // 如果是硬件帧，需要传输到系统内存
        if (frame->format == AV_PIX_FMT_VAAPI) {
            if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0) {
                return false;
            }
            target_frame = sw_frame;
        }

        int width = target_frame->width;
        int height = target_frame->height;
        size_t nv12_size = width * height * 3 / 2;

        // 分配输出缓冲
        if (nv12_buffer_size < nv12_size) {
            nv12_buffer = std::make_unique<uint8_t[]>(nv12_size);
            nv12_buffer_size = nv12_size;
        }

        // 转换为 NV12 格式（如果需要）
        if (target_frame->format == AV_PIX_FMT_NV12) {
            // 已经是 NV12，直接复制
            copyNV12Data(target_frame, nv12_buffer.get(), width, height);
        } else if (target_frame->format == AV_PIX_FMT_YUV420P) {
            // 从 YUV420P 转换为 NV12
            convertYUV420PtoNV12(target_frame, nv12_buffer.get(), width, height);
        } else {
            // 不支持的格式
            return false;
        }

        *out_data = nv12_buffer.get();
        *out_width = width;
        *out_height = height;
        *out_size = nv12_size;

        return true;
    }

    // 复制 NV12 数据
    void copyNV12Data(AVFrame* frame, uint8_t* dst, int width, int height) {
        // 复制 Y 平面
        uint8_t* dst_y = dst;
        for (int i = 0; i < height; i++) {
            memcpy(dst_y + i * width, frame->data[0] + i * frame->linesize[0], width);
        }

        // 复制 UV 平面
        uint8_t* dst_uv = dst + width * height;
        for (int i = 0; i < height / 2; i++) {
            memcpy(dst_uv + i * width, frame->data[1] + i * frame->linesize[1], width);
        }
    }

    // YUV420P 转 NV12
    void convertYUV420PtoNV12(AVFrame* frame, uint8_t* dst, int width, int height) {
        // 复制 Y 平面
        uint8_t* dst_y = dst;
        for (int i = 0; i < height; i++) {
            memcpy(dst_y + i * width, frame->data[0] + i * frame->linesize[0], width);
        }

        // 交错 U 和 V 平面生成 UV 平面
        uint8_t* dst_uv = dst + width * height;
        uint8_t* src_u = frame->data[1];
        uint8_t* src_v = frame->data[2];
        
        for (int i = 0; i < height / 2; i++) {
            for (int j = 0; j < width / 2; j++) {
                dst_uv[i * width + j * 2 + 0] = src_u[i * frame->linesize[1] + j];
                dst_uv[i * width + j * 2 + 1] = src_v[i * frame->linesize[2] + j];
            }
        }
    }
};

// ================ N-API 绑定 ================

class VaapiDecoderWrapper : public Napi::ObjectWrap<VaapiDecoderWrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "VaapiDecoder", {
            InstanceMethod("initFromFile", &VaapiDecoderWrapper::InitFromFile),
            InstanceMethod("initFromBuffer", &VaapiDecoderWrapper::InitFromBuffer),
            InstanceMethod("decodeFrame", &VaapiDecoderWrapper::DecodeFrame),
            InstanceMethod("decodePacket", &VaapiDecoderWrapper::DecodePacket),
            InstanceMethod("getVideoInfo", &VaapiDecoderWrapper::GetVideoInfo),
            InstanceMethod("getLastError", &VaapiDecoderWrapper::GetLastError),
            InstanceMethod("close", &VaapiDecoderWrapper::Close),
        });

        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        exports.Set("VaapiDecoder", func);
        return exports;
    }

    VaapiDecoderWrapper(const Napi::CallbackInfo& info) 
        : Napi::ObjectWrap<VaapiDecoderWrapper>(info) {
        decoder_ = std::make_unique<VaapiDecoder>();
    }

private:
    std::unique_ptr<VaapiDecoder> decoder_;

    // 从文件初始化
    Napi::Value InitFromFile(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected filename string").ThrowAsJavaScriptException();
            return env.Null();
        }

        std::string filename = info[0].As<Napi::String>().Utf8Value();
        bool success = decoder_->initFromFile(filename);

        return Napi::Boolean::New(env, success);
    }

    // 从缓冲区初始化
    Napi::Value InitFromBuffer(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsString()) {
            Napi::TypeError::New(env, "Expected (buffer, codec_name)").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
        std::string codec_name = info[1].As<Napi::String>().Utf8Value();

        bool success = decoder_->initFromBuffer(buffer.Data(), buffer.Length(), codec_name);

        return Napi::Boolean::New(env, success);
    }

    // 解码一帧（从文件）
    Napi::Value DecodeFrame(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        uint8_t* data = nullptr;
        int width = 0, height = 0;
        size_t size = 0;

        bool success = decoder_->decodeFrame(&data, &width, &height, &size);

        if (!success) {
            return env.Null();
        }

        // 创建返回对象
        Napi::Object result = Napi::Object::New(env);
        
        // 复制数据到 Node.js Buffer
        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, data, size);
        
        result.Set("data", buffer);
        result.Set("width", Napi::Number::New(env, width));
        result.Set("height", Napi::Number::New(env, height));
        result.Set("format", Napi::String::New(env, "nv12"));

        return result;
    }

    // 解码数据包（从内存）
    Napi::Value DecodePacket(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected packet buffer").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Buffer<uint8_t> packet = info[0].As<Napi::Buffer<uint8_t>>();

        uint8_t* data = nullptr;
        int width = 0, height = 0;
        size_t size = 0;

        bool success = decoder_->decodePacket(packet.Data(), packet.Length(), 
                                             &data, &width, &height, &size);

        if (!success) {
            return env.Null();
        }

        // 创建返回对象
        Napi::Object result = Napi::Object::New(env);
        
        // 复制数据到 Node.js Buffer
        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, data, size);
        
        result.Set("data", buffer);
        result.Set("width", Napi::Number::New(env, width));
        result.Set("height", Napi::Number::New(env, height));
        result.Set("format", Napi::String::New(env, "nv12"));

        return result;
    }

    // 获取视频信息
    Napi::Value GetVideoInfo(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        int width = 0, height = 0, fps_num = 0, fps_den = 0;
        std::string codec_name;

        bool success = decoder_->getVideoInfo(&width, &height, &codec_name, &fps_num, &fps_den);

        if (!success) {
            return env.Null();
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("width", Napi::Number::New(env, width));
        result.Set("height", Napi::Number::New(env, height));
        result.Set("codec", Napi::String::New(env, codec_name));
        result.Set("fps", Napi::Number::New(env, fps_den > 0 ? (double)fps_num / fps_den : 0));

        return result;
    }

    // 获取最后的错误信息
    Napi::Value GetLastError(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        std::string error = decoder_->getLastError();
        return Napi::String::New(env, error);
    }

    // 关闭解码器
    Napi::Value Close(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        decoder_->cleanup();
        return env.Undefined();
    }
};

// 模块初始化
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return VaapiDecoderWrapper::Init(env, exports);
}

NODE_API_MODULE(vaapi_decoder, Init)
