/**
 * 纯 VA-API H.265 解码器
 * 不依赖 FFmpeg，直接使用 VA-API
 */
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <memory>
#include <napi.h>
#include <stdint.h>
#include <string>
#include <unistd.h>
#include <vector>
typedef void *HMY_DECODER;
typedef HMY_DECODER (*CreateDecoderFn)(const char *);
typedef int (*DecodeFrameFn)(HMY_DECODER, uint8_t **, int *, int *, size_t *);

CreateDecoderFn CreateDecoder;
DecodeFrameFn DecodeFrame;
void *handle = 0;
int InitSo() {

  if (handle)
    return 0; // 已经初始化过

  const char *libpath = "/home/likp/work/ffmpeg_for_node/libvaapi_decoder.so";

  handle = dlopen(libpath, RTLD_NOW);
  if (!handle) {
    std::fprintf(stderr, "dlopen('%s') failed: %s\n", libpath, dlerror());
    return 2;
  }

  dlerror();
  CreateDecoder = (CreateDecoderFn)dlsym(handle, "CreateDecoder");
  const char *err = dlerror();
  if (err) {
    std::fprintf(stderr, "dlsym CreateDecoder failed: %s\n", err);
    dlclose(handle);
    return 3;
  }

  DecodeFrame = (DecodeFrameFn)dlsym(handle, "DecodeFrame");
  err = dlerror();
  if (err) {
    std::fprintf(stderr, "dlsym DecodeFrame failed: %s\n", err);
    dlclose(handle);
    return 4;
  }

  std::fprintf(stderr, "dlsym DecodeFrame succ\n");

  return 0;
}

class PureVaapiDecoder {
private:
public:
  PureVaapiDecoder() {}

  ~PureVaapiDecoder() { cleanup(); }

  void cleanup() {
    if (m_dec) {
      // 假设有一个销毁解码器的函数
      // DestroyDecoder(m_dec);
      m_dec = nullptr;
    }
    if (handle) {
      dlclose(handle);
      handle = nullptr;
    }
  }

  bool initFromFile(const std::string &filename) {
    // cleanup();
    InitSo();
    m_dec = CreateDecoder(filename.c_str());
    if (!m_dec) {
      std::fprintf(stderr, "CreateDecoder returned NULL\n");
      return false;
    }

    std::fprintf(stdout, "CreateTestDecoder succeeded\n");

    uint8_t *data = nullptr;
    int width = 0, height = 0;
    size_t size = 0;

    bool brt = false;
    for (int i = 0; i < 10; i++) {
      int ret = DecodeFrame(m_dec, &data, &width, &height, &size);
      if (ret == 0) {
        printf("Frame decoded: %dx%d, size: %zu\n", width, height, size);
        m_nWidth = width;
        m_nHeight = height;
        brt = true;
        break;
      }
    }
    return brt;
  }
  // 解码一帧
  bool decodeFrame(uint8_t **out_data, int *out_width, int *out_height,
                   size_t *out_size) {
    printf("Decoding frame...\n");
    int ret = DecodeFrame(m_dec, out_data, out_width, out_height, out_size);
    if (ret == 0) {
      return true;
    } else {
      return false;
    }
  }

  bool getVideoInfo(int *out_width, int *out_height) {
    if (m_nWidth > 0 && m_nHeight > 0) {
      *out_width = m_nWidth;
      *out_height = m_nHeight;
      return true;
    } else {
      return false;
    }
  }

private:
  int m_nWidth = 0;
  int m_nHeight = 0;
  HMY_DECODER m_dec;
  // 内部解码函数
};

// N-API 绑定
class PureVaapiDecoderWrapper : public Napi::ObjectWrap<PureVaapiDecoderWrapper> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
      Napi::Function func = DefineClass(
          env, "PureVaapiDecoder",
          {
              InstanceMethod("init", &PureVaapiDecoderWrapper::Init),
              InstanceMethod("decodeFrame",
                             &PureVaapiDecoderWrapper::DecodeFrame),
              InstanceMethod("getVideoInfo",
                             &PureVaapiDecoderWrapper::GetVideoInfo),
          });

      Napi::FunctionReference *constructor = new Napi::FunctionReference();
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

        printf("start DecodeFrame in wrapper...\n");
        bool success = decoder_->decodeFrame(&data, &width, &height, &size);
        printf("end DecodeFrame in wrapper...\n");

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
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return PureVaapiDecoderWrapper::Init(env, exports);
}

NODE_API_MODULE(pure_vaapi_decoder, Init)