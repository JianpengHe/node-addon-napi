#include <napi.h>
#include "zstd.h"
#include <algorithm>
#include <thread>
#include <vector>
#include <string>
#include <cmath>

/**
 * 根据电脑的硬件并发数自动设置压缩 worker 数量。
 * 注意：硬件并发数不一定等于当前进程实际可用的 CPU 数量。
 */
static int GetOptimalThreadCount() {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads <= 1) return 1;
    return static_cast<int>(hardwareThreads);
}

/**
 * 将 Zstd 错误转换成 JavaScript 异常。
 */
static bool CheckZstdError(Napi::Env env, size_t result) {
    if (!ZSTD_isError(result)) return true;
    
    Napi::Error::New(env, ZSTD_getErrorName(result)).ThrowAsJavaScriptException();
    return false;
}

/**
 * 将输出缓冲区追加到结果中。
 */
static void AppendOutput(std::vector<char>& result, const char* data, size_t size) {
    result.insert(result.end(), data, data + size);
}

/**
 * 将 C++ 字节数组复制成 Node.js Buffer。
 */
static Napi::Buffer<char> ToBuffer(Napi::Env env, const std::vector<char>& data) {
    if (data.empty()) return Napi::Buffer<char>::New(env, 0);
    
    return Napi::Buffer<char>::Copy(env, data.data(), data.size());
}

/**
 * Zstd 压缩器。
 * 每个实例对应一个独立的压缩任务。
 * 同一个实例可以连续处理多个输入块，但只能结束一次压缩帧。
 */
class ZstdCompressor : public Napi::ObjectWrap<ZstdCompressor> {
public:
    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "ZstdCompressor", {
            InstanceMethod("compress", &ZstdCompressor::Compress),
            InstanceMethod("finish", &ZstdCompressor::Finish),
            InstanceMethod("close", &ZstdCompressor::Close)
        });

        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();

        exports.Set("ZstdCompressor", func);
        return exports;
    }

    /**
     * 创建压缩器实例。
     * JavaScript 用法：new addon.ZstdCompressor(compressionLevel)
     * compressionLevel 可选，默认值为 3。
     */
    explicit ZstdCompressor(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ZstdCompressor>(info) {
        Napi::Env env = info.Env();

        // 默认使用 3 级压缩，在压缩速度和压缩率之间取得平衡。
        int compressionLevel = 3;

        // 如果传入了压缩级别，则要求它是数字。
        if (info.Length() >= 1 && !info[0].IsUndefined()) {
            if (!info[0].IsNumber()) {
                Napi::TypeError::New(env, "Compression level must be a number").ThrowAsJavaScriptException();
                return;
            }

            const double levelValue = info[0].As<Napi::Number>().DoubleValue();

            if (!std::isfinite(levelValue) ||
                std::floor(levelValue) != levelValue ||
                levelValue < static_cast<double>(ZSTD_minCLevel()) ||
                levelValue > static_cast<double>(ZSTD_maxCLevel())) {
                Napi::RangeError::New(env, "Compression level is out of supported range").ThrowAsJavaScriptException();
                return;
            }

            compressionLevel = static_cast<int>(levelValue);
        }

        // 创建当前实例独立的压缩上下文，避免不同任务互相影响。
        cctx = ZSTD_createCCtx();
        if (!cctx) {
            Napi::Error::New(env, "Failed to create Zstd compression context").ThrowAsJavaScriptException();
            return;
        }

        // 设置压缩级别。级别越高通常压缩率越高，但压缩速度也可能越慢。
        size_t result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compressionLevel);
        if (ZSTD_isError(result)) {
            ZSTD_freeCCtx(cctx);
            cctx = nullptr;
            Napi::Error::New(env, ZSTD_getErrorName(result)).ThrowAsJavaScriptException();
            return;
        }

        // 获取可用的线程数
        int threadCount = GetOptimalThreadCount();

        // 检查 JavaScript 端是否传入了可选的第二个参数（自定义线程数）
        if (info.Length() >= 2 && !info[1].IsUndefined()) {
            // 校验参数类型是否为数字
            if (!info[1].IsNumber()) {
                Napi::TypeError::New(env, "Thread count must be a number").ThrowAsJavaScriptException();
                return;
            }

            const double threadCountValue = info[1].As<Napi::Number>().DoubleValue();

            // 校验数值是否合法：必须是有限数、整数，且至少为 1
            if (!std::isfinite(threadCountValue) || std::floor(threadCountValue) != threadCountValue || threadCountValue < 0) {
                Napi::RangeError::New(env, "Thread count is out of supported range").ThrowAsJavaScriptException();
                return;
            }

            // 采纳用户自定义的线程数，防止用户输入的线程数超过硬件上限
             if(threadCountValue > 0 && threadCountValue < threadCount) threadCount = static_cast<int>(threadCountValue);
        }

        // 根据最终确定的线程数设置 Zstd worker 数量。
        // 此设置要求当前编译的 Zstd 库已启用了多线程支持。
        result = ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, threadCount);
        if (ZSTD_isError(result)) {
            ZSTD_freeCCtx(cctx);
            cctx = nullptr;
            Napi::Error::New(env, ZSTD_getErrorName(result)).ThrowAsJavaScriptException();
        }
    }

    ~ZstdCompressor() {
        Release();
    }

private:
    ZSTD_CCtx* cctx = nullptr;
    bool finished = false;

    // 释放压缩上下文；允许在 close() 和析构函数中重复调用。
    void Release() {
        if (cctx) {
            ZSTD_freeCCtx(cctx);
            cctx = nullptr;
        }
    }

    /**
     * 压缩一个输入块。
     * 这里只使用 ZSTD_e_continue，不结束当前压缩帧；
     * 所有输入块处理完成后，必须再调用 finish() 输出帧尾。
     */
    Napi::Value Compress(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (!cctx || finished) {
            Napi::Error::New(env, "Compressor is closed or already finished").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected a Buffer input").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Buffer<char> inputBuffer = info[0].As<Napi::Buffer<char>>();
        ZSTD_inBuffer input = { inputBuffer.Data(), inputBuffer.Length(), 0 };

        std::vector<char> result;
        std::vector<char> tempBuffer(ZSTD_CStreamOutSize());

        // 循环直到当前输入块全部被压缩器消费。
        while (input.pos < input.size) {
            ZSTD_outBuffer output = { tempBuffer.data(), tempBuffer.size(), 0 };

            size_t remaining = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_continue);
            if (!CheckZstdError(env, remaining)) return env.Null();
            

            AppendOutput(result, tempBuffer.data(), output.pos);
        }

        return ToBuffer(env, result);
    }

    /**
     * 结束当前压缩帧，并取出所有剩余压缩数据。
     * ZSTD_e_end 必须持续调用到返回值为 0，
     * 否则输出结果可能缺少最后的帧尾数据。
     */
    Napi::Value Finish(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (!cctx || finished) {
            Napi::Error::New(env, "Compressor is closed or already finished").ThrowAsJavaScriptException();
            return env.Null();
        }

        std::vector<char> result;
        std::vector<char> tempBuffer(ZSTD_CStreamOutSize());

        // 必须持续调用 ZSTD_e_end，直到返回 0。
        size_t remaining = 1;
        while (remaining != 0) {
            ZSTD_inBuffer input = { nullptr, 0, 0 };
            ZSTD_outBuffer output = { tempBuffer.data(), tempBuffer.size(), 0 };

            remaining = ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_end);
            if (!CheckZstdError(env, remaining)) return env.Null();
            
            AppendOutput(result, tempBuffer.data(), output.pos);
        }

        finished = true;
        return ToBuffer(env, result);
    }

    // 主动释放压缩上下文；调用后不能继续压缩。
    Napi::Value Close(const Napi::CallbackInfo& info) {
        Release();
        return info.Env().Undefined();
    }
};

Napi::FunctionReference ZstdCompressor::constructor;

/**
 * Zstd 解压器。
 * 支持分块输入，并在 finish() 时检查最后一个压缩帧是否完整。
 */
class ZstdDecompressor : public Napi::ObjectWrap<ZstdDecompressor> {
public:
    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "ZstdDecompressor", {
            InstanceMethod("decompress", &ZstdDecompressor::Decompress),
            InstanceMethod("finish", &ZstdDecompressor::Finish),
            InstanceMethod("close", &ZstdDecompressor::Close)
        });

        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();

        exports.Set("ZstdDecompressor", func);
        return exports;
    }

    /**
     * 创建独立的解压上下文。
     * 解压不需要指定压缩级别或 worker 数量。
     */
    explicit ZstdDecompressor(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ZstdDecompressor>(info) {
        dctx = ZSTD_createDCtx();
        if (!dctx) Napi::Error::New(info.Env(), "Failed to create Zstd decompression context").ThrowAsJavaScriptException();
        
    }

    ~ZstdDecompressor() {
        Release();
    }

private:
    ZSTD_DCtx* dctx = nullptr;

    // Zstd 返回 0 表示一个完整帧已经结束。
    size_t lastRemaining = 0;
    bool hasInput = false;
    bool finished = false;

    // 释放解压上下文；允许在 close() 和析构函数中重复调用。
    void Release() {
        if (dctx) {
            ZSTD_freeDCtx(dctx);
            dctx = nullptr;
        }
    }

    /**
     * 解压一个输入块。
     * 输入可能只包含压缩帧的一部分，因此需要保留 dctx 状态，
     * 等后续输入块到达后继续解压。
     */
    Napi::Value Decompress(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (!dctx || finished) {
            Napi::Error::New(env, "Decompressor is closed or already finished").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected a Buffer input").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Buffer<char> inputBuffer = info[0].As<Napi::Buffer<char>>();
        ZSTD_inBuffer input = { inputBuffer.Data(), inputBuffer.Length(), 0 };

        if (input.size > 0) hasInput = true;
        

        std::vector<char> result;
        std::vector<char> tempBuffer(ZSTD_DStreamOutSize());

        // 当输出缓冲区写满时，即使输入块已消费完，也继续取出剩余输出。
        bool outputFull = false;

        do {
            ZSTD_outBuffer output = { tempBuffer.data(), tempBuffer.size(), 0 };

            lastRemaining = ZSTD_decompressStream(dctx, &output, &input);
            if (!CheckZstdError(env, lastRemaining)) return env.Null();
            

            AppendOutput(result, tempBuffer.data(), output.pos);

            outputFull = output.pos == output.size;
        } while (input.pos < input.size || outputFull);

        return ToBuffer(env, result);
    }

    /**
     * 检查解压是否正常结束。
     * 如果最后一个压缩帧尚未完整结束，lastRemaining 通常不为 0，
     * 此时认为输入数据被截断或不完整。
     */
    Napi::Value Finish(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (!dctx || finished) {
            Napi::Error::New(env, "Decompressor is closed or already finished").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (!hasInput || lastRemaining != 0) {
            Napi::Error::New(env, "Incomplete or empty Zstd frame").ThrowAsJavaScriptException();
            return env.Null();
        }

        finished = true;
        return Napi::Buffer<char>::New(env, 0);
    }

    // 主动释放解压上下文；调用后不能继续解压。
    Napi::Value Close(const Napi::CallbackInfo& info) {
        Release();
        return info.Env().Undefined();
    }
};

Napi::FunctionReference ZstdDecompressor::constructor;

/**
 * 注册 Node.js addon 对外暴露的压缩器和解压器类。
 */
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    ZstdCompressor::Init(env, exports);
    ZstdDecompressor::Init(env, exports);
    return exports;
}

NODE_API_MODULE(zstd, Init)