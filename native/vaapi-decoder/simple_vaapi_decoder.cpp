/**
 * 简化版 VA-API 解码器
 * 直接读取 H264/H265 裸流，通过 NAL 单元分割解码
 */
#include <napi.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
}

#include <memory>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>

class SimpleVaapiDecoder {
private:
    AVCodecContext* codec_ctx = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw_frame = nullptr;
    AVPacket* packet = nullptr;
    
    int drm_fd = -1;
    bool initialized = false;
    bool use_hw_accel = false;
    std::string last_error;
    
    // 文件缓冲
    std::vector<uint8_t> file_buffer;
    size_t buffer_pos = 0;
    
    // NV12 输出缓冲
    std::unique_ptr<uint8_t[]> nv12_buffer;
    size_t nv12_buffer_size = 0;
    
    // 视频信息
    int video_width = 0;
    int video_height = 0;

public:
    SimpleVaapiDecoder() {
        frame = av_frame_alloc();
        sw_frame = av_frame_alloc();
        packet = av_packet_alloc();
    }

    ~SimpleVaapiDecoder() {
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
        if (hw_device_ctx) {
            av_buffer_unref(&hw_device_ctx);
            hw_device_ctx = nullptr;
        }
        if (drm_fd >= 0) {
            close(drm_fd);
            drm_fd = -1;
        }
        file_buffer.clear();
        buffer_pos = 0;
        initialized = false;
    }

    // 初始化 VA-API 设备（可选）
    bool initVAAPI() {
        const char* device_path = "/dev/dri/renderD128";
        
        drm_fd = open(device_path, O_RDWR);
        if (drm_fd < 0) {
            last_error = "Cannot open DRM device (using software decode)";
            return false;
        }

        int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
                                         device_path, nullptr, 0);
        if (ret < 0) {
            close(drm_fd);
            drm_fd = -1;
            last_error = "Cannot create VA-API context (using software decode)";
            return false;
        }

        return true;
    }

    // 从文件初始化解码器
    bool initFromFile(const std::string& filename, const std::string& codec_name) {
        cleanup();

        // 尝试初始化硬件加速（失败也没关系）
        use_hw_accel = initVAAPI();
        if (use_hw_accel) {
            fprintf(stderr, "Hardware acceleration enabled\n");
        } else {
            fprintf(stderr, "Using software decoding: %s\n", last_error.c_str());
        }

        // 打开并读取整个文件到内存
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

        // 查找解码器
        AVCodecID codec_id;
        if (codec_name == "h264" || codec_name == "H264") {
            codec_id = AV_CODEC_ID_H264;
        } else if (codec_name == "hevc" || codec_name == "h265" || codec_name == "H265") {
            codec_id = AV_CODEC_ID_HEVC;
        } else {
            last_error = "Unsupported codec: " + codec_name;
            return false;
        }

        const AVCodec* decoder = avcodec_find_decoder(codec_id);
        if (!decoder) {
            last_error = "Decoder not found for codec: " + codec_name;
            return false;
        }

        // 创建解码器上下文
        codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            last_error = "Cannot allocate codec context";
            return false;
        }

        // 设置硬件加速（如果可用）
        if (use_hw_accel && hw_device_ctx) {
            codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
            codec_ctx->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) -> enum AVPixelFormat {
                for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
                    if (*p == AV_PIX_FMT_VAAPI) return *p;
                }
                return AV_PIX_FMT_NONE;
            };
        }

        // 打开解码器
        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            last_error = "Cannot open decoder";
            cleanup();
            return false;
        }

        initialized = true;
        fprintf(stderr, "Decoder initialized successfully\n");
        return true;
    }

    // 查找下一个 NAL 单元（0x00 0x00 0x00 0x01 或 0x00 0x00 0x01）
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
                nal_start = i;
                buffer_pos = i + 4;
                found_start = true;
                break;
            }
            // 检查 0x00 0x00 0x01 (3字节起始码)
            else if (file_buffer[i] == 0 && file_buffer[i+1] == 0 && file_buffer[i+2] == 1) {
                nal_start = i;
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

    // 解码下一帧
    bool decodeFrame(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        if (!initialized) {
            last_error = "Decoder not initialized";
            return false;
        }

        // 持续发送 NAL 单元直到获得一帧
        while (true) {
            size_t nal_start, nal_end;
            
            // 查找下一个 NAL 单元
            if (findNextNAL(nal_start, nal_end)) {
                size_t nal_size = nal_end - nal_start;
                
                // 设置数据包
                packet->data = file_buffer.data() + nal_start;
                packet->size = nal_size;

                // 发送到解码器
                int ret = avcodec_send_packet(codec_ctx, packet);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    // 忽略错误，继续下一个 NAL
                    continue;
                }

                // 尝试接收帧
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == 0) {
                    // 成功解码一帧
                    if (video_width == 0) {
                        video_width = frame->width;
                        video_height = frame->height;
                        fprintf(stderr, "Video resolution: %dx%d\n", video_width, video_height);
                    }
                    return extractNV12Frame(out_data, out_width, out_height, out_size);
                } else if (ret != AVERROR(EAGAIN)) {
                    // 解码错误
                    continue;
                }
            } else {
                // 文件结束，刷新解码器
                avcodec_send_packet(codec_ctx, nullptr);
                int ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == 0) {
                    return extractNV12Frame(out_data, out_width, out_height, out_size);
                }
                return false; // 真正的结束
            }
        }
    }

    // 提取 NV12 帧数据
    bool extractNV12Frame(uint8_t** out_data, int* out_width, int* out_height, size_t* out_size) {
        AVFrame* target_frame = frame;

        // 如果是硬件帧，传输到系统内存
        if (frame->format == AV_PIX_FMT_VAAPI) {
            if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0) {
                last_error = "Failed to transfer hardware frame";
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

        // 转换为 NV12
        if (target_frame->format == AV_PIX_FMT_NV12) {
            copyNV12Data(target_frame, nv12_buffer.get(), width, height);
        } else if (target_frame->format == AV_PIX_FMT_YUV420P) {
            convertYUV420PtoNV12(target_frame, nv12_buffer.get(), width, height);
        } else {
            last_error = "Unsupported pixel format";
            return false;
        }

        *out_data = nv12_buffer.get();
        *out_width = width;
        *out_height = height;
        *out_size = nv12_size;
        return true;
    }

    void copyNV12Data(AVFrame* frame, uint8_t* dst, int width, int height) {
        uint8_t* dst_y = dst;
        for (int i = 0; i < height; i++) {
            memcpy(dst_y + i * width, frame->data[0] + i * frame->linesize[0], width);
        }
        uint8_t* dst_uv = dst + width * height;
        for (int i = 0; i < height / 2; i++) {
            memcpy(dst_uv + i * width, frame->data[1] + i * frame->linesize[1], width);
        }
    }

    void convertYUV420PtoNV12(AVFrame* frame, uint8_t* dst, int width, int height) {
        uint8_t* dst_y = dst;
        for (int i = 0; i < height; i++) {
            memcpy(dst_y + i * width, frame->data[0] + i * frame->linesize[0], width);
        }
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

    bool getVideoInfo(int* width, int* height) {
        if (!initialized || video_width == 0) return false;
        *width = video_width;
        *height = video_height;
        return true;
    }

    std::string getLastError() const {
        return last_error;
    }

    void reset() {
        buffer_pos = 0;
        if (codec_ctx) {
            avcodec_flush_buffers(codec_ctx);
        }
    }
};

// N-API 绑定
class SimpleVaapiDecoderWrapper : public Napi::ObjectWrap<SimpleVaapiDecoderWrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "SimpleVaapiDecoder", {
            InstanceMethod("init", &SimpleVaapiDecoderWrapper::Init),
            InstanceMethod("decodeFrame", &SimpleVaapiDecoderWrapper::DecodeFrame),
            InstanceMethod("getVideoInfo", &SimpleVaapiDecoderWrapper::GetVideoInfo),
            InstanceMethod("getLastError", &SimpleVaapiDecoderWrapper::GetLastError),
            InstanceMethod("reset", &SimpleVaapiDecoderWrapper::Reset),
            InstanceMethod("close", &SimpleVaapiDecoderWrapper::Close),
        });

        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        exports.Set("SimpleVaapiDecoder", func);
        return exports;
    }

    SimpleVaapiDecoderWrapper(const Napi::CallbackInfo& info) 
        : Napi::ObjectWrap<SimpleVaapiDecoderWrapper>(info) {
        decoder_ = std::make_unique<SimpleVaapiDecoder>();
    }

private:
    std::unique_ptr<SimpleVaapiDecoder> decoder_;

    Napi::Value Init(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
            Napi::TypeError::New(env, "Expected (filename, codec)").ThrowAsJavaScriptException();
            return env.Null();
        }
        std::string filename = info[0].As<Napi::String>().Utf8Value();
        std::string codec = info[1].As<Napi::String>().Utf8Value();
        bool success = decoder_->initFromFile(filename, codec);
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
    return SimpleVaapiDecoderWrapper::Init(env, exports);
}

NODE_API_MODULE(simple_vaapi_decoder, Init)
