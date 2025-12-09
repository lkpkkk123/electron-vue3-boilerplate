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
};

std::map<std::string, SharedMemoryManager::SharedMemoryInfo> 
    SharedMemoryManager::sharedMemories;

// 模块初始化
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("create", Napi::Function::New(env, SharedMemoryManager::Create));
    exports.Set("write", Napi::Function::New(env, SharedMemoryManager::Write));
    exports.Set("read", Napi::Function::New(env, SharedMemoryManager::Read));
    exports.Set("close", Napi::Function::New(env, SharedMemoryManager::Close));
    exports.Set("mapSharedMemory",
                Napi::Function::New(env, SharedMemoryManager::MapSharedMemory));
    exports.Set("getMappedView",
                Napi::Function::New(env, SharedMemoryManager::GetMappedView));
    return exports;
}

NODE_API_MODULE(shared_memory, Init)
