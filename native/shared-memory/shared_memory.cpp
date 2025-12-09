/**
 * 共享内存 Native Addon
 * 支持 Linux/macOS 的 POSIX 共享内存
 */
#include <napi.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <map>

// 共享内存管理器
class SharedMemoryManager {
private:
    struct SharedMemoryInfo {
        void* ptr;
        size_t size;
        int fd;
    };
    
    static std::map<std::string, SharedMemoryInfo> sharedMemories;

    // 缓存的图像 Buffer 和颜色顺序状态
    static Napi::Reference<Napi::Buffer<uint8_t>> *cachedImageBuffer;
    static int currentColorOrder; // 0=RGB, 1=GBR, 2=BRG
    static int cachedWidth;
    static int cachedHeight;
    static int m_nLine;

  public:
    // 创建或打开共享内存
    static Napi::Value Create(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsNumber()) {
            Napi::TypeError::New(env, "Expected (name: string, size: number)")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        std::string name = info[0].As<Napi::String>().Utf8Value();
        size_t size = info[1].As<Napi::Number>().Uint32Value();
        
        // 确保名字以 / 开头
        if (name[0] != '/') {
            name = "/" + name;
        }
        
        // 创建共享内存
        int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd == -1) {
            Napi::Error::New(env, "Failed to create shared memory")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // 设置大小
        if (ftruncate(fd, size) == -1) {
            close(fd);
            Napi::Error::New(env, "Failed to set shared memory size")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // 映射到内存
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            Napi::Error::New(env, "Failed to map shared memory")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // 保存信息
        SharedMemoryInfo info_struct = { ptr, size, fd };
        sharedMemories[name] = info_struct;
        
        Napi::Object result = Napi::Object::New(env);
        result.Set("name", name);
        result.Set("size", size);
        result.Set("success", true);
        
        return result;
    }
    
    // 写入数据到共享内存
    static Napi::Value Write(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 2 || !info[0].IsString() || !info[1].IsBuffer()) {
            Napi::TypeError::New(env, "Expected (name: string, data: Buffer)")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        std::string name = info[0].As<Napi::String>().Utf8Value();
        if (name[0] != '/') {
            name = "/" + name;
        }
        
        auto it = sharedMemories.find(name);
        if (it == sharedMemories.end()) {
            Napi::Error::New(env, "Shared memory not found")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        Napi::Buffer<uint8_t> buffer = info[1].As<Napi::Buffer<uint8_t>>();
        size_t dataSize = buffer.Length();
        
        if (dataSize > it->second.size) {
            Napi::Error::New(env, "Data size exceeds shared memory size")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // 写入数据
        memcpy(it->second.ptr, buffer.Data(), dataSize);
        
        return Napi::Number::New(env, dataSize);
    }
    
    // 从共享内存读取数据
    static Napi::Value Read(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected (name: string)")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        std::string name = info[0].As<Napi::String>().Utf8Value();
        if (name[0] != '/') {
            name = "/" + name;
        }
        
        auto it = sharedMemories.find(name);

        // 如果在当前进程中未找到，尝试打开已存在的共享内存
        if (it == sharedMemories.end()) {
          // 尝试打开已存在的共享内存
          int fd = shm_open(name.c_str(), O_RDWR, 0666);
          if (fd == -1) {
            Napi::Error::New(env, "Shared memory not found")
                .ThrowAsJavaScriptException();
            return env.Null();
          }

          // 获取大小
          struct stat st;
          if (fstat(fd, &st) == -1) {
            close(fd);
            Napi::Error::New(env, "Failed to get shared memory size")
                .ThrowAsJavaScriptException();
            return env.Null();
          }
          size_t size = st.st_size;

          // 映射到内存
          void *ptr =
              mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
          if (ptr == MAP_FAILED) {
            close(fd);
            Napi::Error::New(env, "Failed to map shared memory")
                .ThrowAsJavaScriptException();
            return env.Null();
          }

          // 保存到映射表
          SharedMemoryInfo info_struct = {ptr, size, fd};
          sharedMemories[name] = info_struct;
          it = sharedMemories.find(name);
        }

        // auto finalizer = [](Napi::Env env, void *data) {

        // };
        // Napi::Buffer<char> buffer =
        //     Napi::Buffer<char>::New(env,
        //                             (char *)static_cast<uint8_t *>(
        //                                 it->second.ptr), //
        //                                 直接传入共享内存指针
        //                             it->second.size, finalizer);
        Napi::Buffer<char> buffer = Napi::Buffer<char>::Copy(
            env, (char *)static_cast<uint8_t *>(it->second.ptr),
            it->second.size);

        return buffer;
    }
    
    // 关闭共享内存
    static Napi::Value Close(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected (name: string)")
                .ThrowAsJavaScriptException();
            return env.Null();
        }
        
        std::string name = info[0].As<Napi::String>().Utf8Value();
        if (name[0] != '/') {
            name = "/" + name;
        }
        
        auto it = sharedMemories.find(name);
        if (it != sharedMemories.end()) {
            munmap(it->second.ptr, it->second.size);
            close(it->second.fd);
            shm_unlink(name.c_str());
            sharedMemories.erase(it);
        }
        
        return Napi::Boolean::New(env, true);
    }

    // 映射共享内存到当前进程（用于渲染进程）
    static Napi::Value MapSharedMemory(const Napi::CallbackInfo &info) {
      Napi::Env env = info.Env();

      if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected (name: string)")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      std::string name = info[0].As<Napi::String>().Utf8Value();
      if (name[0] != '/') {
        name = "/" + name;
      }

      // 如果已经映射过，直接返回成功
      auto it = sharedMemories.find(name);
      if (it != sharedMemories.end()) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", true);
        result.Set("size", Napi::Number::New(env, it->second.size));
        return result;
      }

      // 打开已存在的共享内存
      int fd = shm_open(name.c_str(), O_RDWR, 0666);
      if (fd == -1) {
        Napi::Error::New(env, "Failed to open shared memory")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      // 获取大小
      struct stat st;
      if (fstat(fd, &st) == -1) {
        close(fd);
        Napi::Error::New(env, "Failed to get shared memory size")
            .ThrowAsJavaScriptException();
        return env.Null();
      }
      size_t size = st.st_size;

      // 映射到内存
      void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (ptr == MAP_FAILED) {
        close(fd);
        Napi::Error::New(env, "Failed to map shared memory")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      // 保存到映射表
      SharedMemoryInfo info_struct = {ptr, size, fd};
      sharedMemories[name] = info_struct;

      Napi::Object result = Napi::Object::New(env);
      result.Set("success", true);
      result.Set("size", Napi::Number::New(env, size));
      return result;
    }

    // 从映射的共享内存创建零拷贝视图
    static Napi::Value GetMappedView(const Napi::CallbackInfo &info) {
      Napi::Env env = info.Env();

      if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected (name: string)")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      std::string name = info[0].As<Napi::String>().Utf8Value();
      if (name[0] != '/') {
        name = "/" + name;
      }

      auto it = sharedMemories.find(name);
      if (it == sharedMemories.end()) {
        Napi::Error::New(env,
                         "Shared memory not mapped. Call mapSharedMemory first")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      // 返回指针地址和大小，让 JavaScript 层处理
      Napi::Object result = Napi::Object::New(env);
      result.Set("address", Napi::Number::New(env, reinterpret_cast<uintptr_t>(
                                                       it->second.ptr)));
      result.Set("size", Napi::Number::New(env, it->second.size));

      return result;
    }

    // 填充测试图像数据（RGB格式：上1/3红色，中1/3绿色，下1/3蓝色）
    static Napi::Value Fill(const Napi::CallbackInfo &info) {
      Napi::Env env = info.Env();

      if (info.Length() < 3 || !info[0].IsBuffer() || !info[1].IsNumber() ||
          !info[2].IsNumber()) {
        Napi::TypeError::New(
            env, "Expected (buffer: Buffer, width: number, height: number)")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
      int width = info[1].As<Napi::Number>().Int32Value();
      int height = info[2].As<Napi::Number>().Int32Value();

      uint8_t *data = buffer.Data();
      size_t bufferSize = buffer.Length();
      size_t expectedSize = width * height * 3; // RGB 3 bytes per pixel

      if (expectedSize > bufferSize) {
        Napi::Error::New(env, "Buffer size too small for given dimensions")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      int rowsPerSection = height / 3;

      // 填充上1/3：红色 (R=255, G=0, B=0)
      for (int y = 0; y < rowsPerSection; y++) {
        for (int x = 0; x < width; x++) {
          int index = (y * width + x) * 3;
          data[index + 0] = 255; // R
          data[index + 1] = 0;   // G
          data[index + 2] = 0;   // B
        }
      }

      // 填充中1/3：绿色 (R=0, G=255, B=0)
      for (int y = rowsPerSection; y < rowsPerSection * 2; y++) {
        for (int x = 0; x < width; x++) {
          int index = (y * width + x) * 3;
          data[index + 0] = 0;   // R
          data[index + 1] = 255; // G
          data[index + 2] = 0;   // B
        }
      }

      // 填充下1/3：蓝色 (R=0, G=0, B=255)
      for (int y = rowsPerSection * 2; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int index = (y * width + x) * 3;
          data[index + 0] = 0;   // R
          data[index + 1] = 0;   // G
          data[index + 2] = 255; // B
        }
      }

      return Napi::Number::New(env, expectedSize);
    }

    // 获取图像 Buffer（YUV NV12 格式：Y平面 + 交错UV平面）
    static Napi::Value GetImg(const Napi::CallbackInfo &info) {
      Napi::Env env = info.Env();

      if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected (width: number, height: number)")
            .ThrowAsJavaScriptException();
        return env.Null();
      }

      int width = info[0].As<Napi::Number>().Int32Value();
      int height = info[1].As<Napi::Number>().Int32Value();
      // NV12 格式: Y平面 (width * height) + UV平面 (width * height / 2)
      size_t bufferSize = width * height * 3 / 2;

      // 如果尺寸变化或首次调用，重新创建 Buffer
      if (!cachedImageBuffer || cachedWidth != width ||
          cachedHeight != height) {
        if (cachedImageBuffer) {
          cachedImageBuffer->Reset();
          delete cachedImageBuffer;
        }

        Napi::Buffer<uint8_t> newBuffer =
            Napi::Buffer<uint8_t>::New(env, bufferSize);
        cachedImageBuffer = new Napi::Reference<Napi::Buffer<uint8_t>>(
            Napi::Persistent(newBuffer));
        cachedWidth = width;
        cachedHeight = height;
        currentColorOrder = 0;
        m_nLine = 0; // 重置行号

        // 初始化 NV12 数据（Y=16黑色, UV=128中性灰）
        uint8_t *data = cachedImageBuffer->Value().Data();
        memset(data, 16, width * height);                       // Y 平面
        memset(data + width * height, 128, width * height / 2); // UV 平面
      }

      // 获取 Buffer 数据指针
      uint8_t *data = cachedImageBuffer->Value().Data();
      uint8_t *yPlane = data;
      uint8_t *uvPlane = data + width * height;
      int rowsPerSection = height / 3;

      // YUV 颜色值（标准 BT.601）
      // 红色: Y=82,  U=90,  V=240
      // 绿色: Y=145, U=54,  V=34
      // 蓝色: Y=41,  U=240, V=110
      struct YUVColor {
        uint8_t y, u, v;
      };
      YUVColor colors[3] = {
          {82, 90, 240}, // 红色
          {145, 54, 34}, // 绿色
          {41, 240, 110} // 蓝色
      };

      // 填充一行（循环遍历所有行）
      if (m_nLine >= height) {
        m_nLine = 0; // 重置到第一行
      }

      // 确定当前行应该使用哪种颜色
      int colorIndex = 0;
      if (m_nLine < rowsPerSection) {
        colorIndex = 0; // 红色
      } else if (m_nLine < rowsPerSection * 2) {
        colorIndex = 1; // 绿色
      } else {
        colorIndex = 2; // 蓝色
      }

      YUVColor color = colors[colorIndex];

      // 填充 Y 平面当前行
      for (int x = 0; x < width; x++) {
        yPlane[m_nLine * width + x] = color.y;
      }

      // 填充 UV 平面（每2x2像素块共享一个UV值，所以只在偶数行填充）
      if (m_nLine % 2 == 0) {
        int uvRow = m_nLine / 2;
        for (int x = 0; x < width / 2; x++) {
          uvPlane[uvRow * width + x * 2 + 0] = color.u; // U
          uvPlane[uvRow * width + x * 2 + 1] = color.v; // V
        }
      }

      // 移动到下一行
      m_nLine++;

      return cachedImageBuffer->Value();
    }
};

std::map<std::string, SharedMemoryManager::SharedMemoryInfo> 
    SharedMemoryManager::sharedMemories;

// 初始化静态成员变量
Napi::Reference<Napi::Buffer<uint8_t>> *SharedMemoryManager::cachedImageBuffer =
    nullptr;
int SharedMemoryManager::currentColorOrder = 0;
int SharedMemoryManager::cachedWidth = 0;
int SharedMemoryManager::cachedHeight = 0;
int SharedMemoryManager::m_nLine = 0;
// 模块初始化
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("create", Napi::Function::New(env, SharedMemoryManager::Create));
    exports.Set("write", Napi::Function::New(env, SharedMemoryManager::Write));
    exports.Set("read", Napi::Function::New(env, SharedMemoryManager::Read));
    exports.Set("close", Napi::Function::New(env, SharedMemoryManager::Close));
    exports.Set("fill", Napi::Function::New(env, SharedMemoryManager::Fill));
    exports.Set("getImg",
                Napi::Function::New(env, SharedMemoryManager::GetImg));
    exports.Set("mapSharedMemory",
                Napi::Function::New(env, SharedMemoryManager::MapSharedMemory));
    exports.Set("getMappedView",
                Napi::Function::New(env, SharedMemoryManager::GetMappedView));
    return exports;
}

NODE_API_MODULE(shared_memory, Init)
