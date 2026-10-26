#include <napi.h>
#include "zstd.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

static int GetOptimalThreadCount() {
    const unsigned int n = std::thread::hardware_concurrency();
    return n <= 1 ? 1 : static_cast<int>(n);
}

static void AppendOutput(std::vector<char>& result, const char* data, size_t size) {
    if (size != 0) result.insert(result.end(), data, data + size);
}

static Napi::Buffer<char> ToBuffer(Napi::Env env, const std::vector<char>& data) {
    if (data.empty()) return Napi::Buffer<char>::New(env, 0);
    return Napi::Buffer<char>::Copy(env, data.data(), data.size());
}

struct CodecState {
    enum class Type { Compress, Decompress };

    explicit CodecState(Type codecType) : type(codecType) {}
    ~CodecState() {
        if (cctx) ZSTD_freeCCtx(cctx);
        if (dctx) ZSTD_freeDCtx(dctx);
    }

    Type type;
    ZSTD_CCtx* cctx = nullptr;
    ZSTD_DCtx* dctx = nullptr;

    // Only one async operation may use a state at a time.
    std::atomic<bool> busy{false};
    std::atomic<bool> finished{false};
    bool closed = false;       // Read/written on the JS thread only.

    // Decompress-stream state. Accessed only by the serialized worker operation.
    size_t lastRemaining = 0;
    bool hasInput = false;
};

enum class Operation { Compress, Decompress, Finish };

class CodecWorker final : public Napi::AsyncWorker {
public:
    CodecWorker(Napi::Function callback,
                std::shared_ptr<CodecState> state,
                Operation operation,
                std::vector<char> input)
        : Napi::AsyncWorker(callback),
          state_(std::move(state)),
          operation_(operation),
          input_(std::move(input)) {}

    void Execute() override {
        try {
            if (state_->type == CodecState::Type::Compress) {
                if (operation_ == Operation::Compress) CompressChunk();
                else if (operation_ == Operation::Finish) FinishCompress();
                else throw std::runtime_error("Invalid operation for compressor");
            } else {
                if (operation_ == Operation::Decompress) DecompressChunk();
                else if (operation_ == Operation::Finish) FinishDecompress();
                else throw std::runtime_error("Invalid operation for decompressor");
            }
        } catch (const std::exception& e) {
            error_ = e.what();
            SetError(error_);
        } catch (...) {
            error_ = "Unknown native Zstd error";
            SetError(error_);
        }
    }

    void OnOK() override {
        state_->busy.store(false);
        if (operation_ == Operation::Finish) state_->finished.store(true);

        Napi::HandleScope scope(Env());
        Napi::Value output = ToBuffer(Env(), output_);
        Callback().Call({Env().Null(), output});
    }

    void OnError(const Napi::Error& error) override {
        state_->busy.store(false);
        Napi::HandleScope scope(Env());
        Callback().Call({error.Value(), Env().Undefined()});
    }

private:
    std::shared_ptr<CodecState> state_;
    Operation operation_;
    std::vector<char> input_;
    std::vector<char> output_;
    std::string error_;

    void CompressChunk() {
        if (!state_->cctx) throw std::runtime_error("Compressor is closed");

        ZSTD_inBuffer input = {input_.data(), input_.size(), 0};
        std::vector<char> temp(ZSTD_CStreamOutSize());

        while (input.pos < input.size) {
            ZSTD_outBuffer output = {temp.data(), temp.size(), 0};
            const size_t result = ZSTD_compressStream2(
                state_->cctx, &output, &input, ZSTD_e_continue);
            if (ZSTD_isError(result)) throw std::runtime_error(ZSTD_getErrorName(result));
            AppendOutput(output_, temp.data(), output.pos);
        }
    }

    void FinishCompress() {
        if (!state_->cctx) throw std::runtime_error("Compressor is closed");

        std::vector<char> temp(ZSTD_CStreamOutSize());
        size_t remaining = 1;
        while (remaining != 0) {
            ZSTD_inBuffer input = {nullptr, 0, 0};
            ZSTD_outBuffer output = {temp.data(), temp.size(), 0};
            remaining = ZSTD_compressStream2(
                state_->cctx, &output, &input, ZSTD_e_end);
            if (ZSTD_isError(remaining)) throw std::runtime_error(ZSTD_getErrorName(remaining));
            AppendOutput(output_, temp.data(), output.pos);
        }
    }

    void DecompressChunk() {
        if (!state_->dctx) throw std::runtime_error("Decompressor is closed");

        // 空输入不应覆盖上一次解码结果
        if (input_.empty()) return;

        ZSTD_inBuffer input = {input_.data(), input_.size(), 0};
        if (input.size > 0) state_->hasInput = true;

        std::vector<char> temp(ZSTD_DStreamOutSize());
        bool outputFull = false;
        do {
            ZSTD_outBuffer output = {temp.data(), temp.size(), 0};
            state_->lastRemaining = ZSTD_decompressStream(
                state_->dctx, &output, &input);
            if (ZSTD_isError(state_->lastRemaining)) {
                throw std::runtime_error(ZSTD_getErrorName(state_->lastRemaining));
            }
            AppendOutput(output_, temp.data(), output.pos);
            outputFull = output.pos == output.size;
        } while (input.pos < input.size || (outputFull && state_->lastRemaining != 0));
    }

    void FinishDecompress() {
        if (!state_->dctx) throw std::runtime_error("Decompressor is closed");
        if (!state_->hasInput || state_->lastRemaining != 0) {
            throw std::runtime_error("Incomplete or empty Zstd frame");
        }
        // A successful decompression finish returns an empty Buffer.
    }
};

static bool ValidateCallback(const Napi::CallbackInfo& info, int index) {
    if (info.Length() <= static_cast<size_t>(index) || !info[index].IsFunction()) {
        Napi::TypeError::New(info.Env(), "Expected a callback function as the last argument")
            .ThrowAsJavaScriptException();
        return false;
    }
    return true;
}

static bool CopyInputBuffer(const Napi::CallbackInfo& info, int index,
                            std::vector<char>& output) {
    if (info.Length() <= static_cast<size_t>(index) || !info[index].IsBuffer()) {
        Napi::TypeError::New(info.Env(), "Expected a Buffer input")
            .ThrowAsJavaScriptException();
        return false;
    }
    Napi::Buffer<char> buffer = info[index].As<Napi::Buffer<char>>();
    output.assign(buffer.Data(), buffer.Data() + buffer.Length());
    return true;
}

static bool QueueOperation(const Napi::CallbackInfo& info,
                           const std::shared_ptr<CodecState>& state,
                           Operation operation,
                           int callbackIndex,
                           std::vector<char> input = {}) {
    Napi::Env env = info.Env();
    if (!ValidateCallback(info, callbackIndex)) return false;
    if (state->closed) {
        Napi::Error::New(env, "Codec is closed").ThrowAsJavaScriptException();
        return false;
    }
    if (state->finished.load()) {
        Napi::Error::New(env, "Codec is already finished").ThrowAsJavaScriptException();
        return false;
    }

    bool expected = false;
    if (!state->busy.compare_exchange_strong(expected, true)) {
        Napi::Error::New(env, "Another operation is already running on this codec")
            .ThrowAsJavaScriptException();
        return false;
    }

    Napi::Function callback = info[callbackIndex].As<Napi::Function>();
    auto* worker = new CodecWorker(callback, state, operation, std::move(input));
    worker->Queue();
    return true;
}

} // namespace

class ZstdCompressor : public Napi::ObjectWrap<ZstdCompressor> {
public:
    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "ZstdCompressor", {
            InstanceMethod("transform", &ZstdCompressor::Compress),
            InstanceMethod("flush", &ZstdCompressor::Finish),
            InstanceMethod("destroy", &ZstdCompressor::Close)
        });
        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();
        exports.Set("ZstdCompressor", func);
        return exports;
    }

    explicit ZstdCompressor(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<ZstdCompressor>(info),
          state_(std::make_shared<CodecState>(CodecState::Type::Compress)) {
        Napi::Env env = info.Env();
        int compressionLevel = 3;
        int threadCount = GetOptimalThreadCount();

        if (info.Length() >= 1 && !info[0].IsUndefined()) {
            if (!info[0].IsNumber()) {
                Napi::TypeError::New(env, "Compression level must be a number").ThrowAsJavaScriptException();
                return;
            }
            const double value = info[0].As<Napi::Number>().DoubleValue();
            if (!std::isfinite(value) || std::floor(value) != value ||
                value < static_cast<double>(ZSTD_minCLevel()) ||
                value > static_cast<double>(ZSTD_maxCLevel())) {
                Napi::RangeError::New(env, "Compression level is out of supported range").ThrowAsJavaScriptException();
                return;
            }
            compressionLevel = static_cast<int>(value);
        }

        if (info.Length() >= 2 && !info[1].IsUndefined()) {
            if (!info[1].IsNumber()) {
                Napi::TypeError::New(env, "Thread count must be a number").ThrowAsJavaScriptException();
                return;
            }
            const double value = info[1].As<Napi::Number>().DoubleValue();
            if (!std::isfinite(value) || std::floor(value) != value || value < 0 ||
                value > static_cast<double>(std::numeric_limits<int>::max())) {
                Napi::RangeError::New(env, "Thread count is out of supported range").ThrowAsJavaScriptException();
                return;
            }
            if (value > 0 && value < threadCount) threadCount = static_cast<int>(value);
        }

        state_->cctx = ZSTD_createCCtx();
        if (!state_->cctx) {
            Napi::Error::New(env, "Failed to create Zstd compression context").ThrowAsJavaScriptException();
            return;
        }

        size_t result = ZSTD_CCtx_setParameter(state_->cctx, ZSTD_c_compressionLevel, compressionLevel);
        if (ZSTD_isError(result)) {
            Napi::Error::New(env, ZSTD_getErrorName(result)).ThrowAsJavaScriptException();
            state_->closed = true;
            return;
        }
        result = ZSTD_CCtx_setParameter(state_->cctx, ZSTD_c_nbWorkers, threadCount);
        if (ZSTD_isError(result)) {
            Napi::Error::New(env, ZSTD_getErrorName(result)).ThrowAsJavaScriptException();
            state_->closed = true;
            return;
        }
    }

    ~ZstdCompressor() = default;

private:
    std::shared_ptr<CodecState> state_;

    Napi::Value Compress(const Napi::CallbackInfo& info) {
        std::vector<char> input;
        if (info.Length() < 2 || !info[0].IsBuffer()) {
            Napi::TypeError::New(info.Env(), "Usage: compress(buffer, callback)").ThrowAsJavaScriptException();
            return info.Env().Undefined();
        }
        if (!CopyInputBuffer(info, 0, input)) return info.Env().Undefined();
        QueueOperation(info, state_, Operation::Compress, 1, std::move(input));
        return info.Env().Undefined();
    }

    Napi::Value Finish(const Napi::CallbackInfo& info) {
        QueueOperation(info, state_, Operation::Finish, 0);
        return info.Env().Undefined();
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        if (state_->busy.load()) {
            Napi::Error::New(info.Env(), "Cannot close codec while an operation is running").ThrowAsJavaScriptException();
            return info.Env().Undefined();
        }
        state_->closed = true;
        if (state_->cctx) {
            ZSTD_freeCCtx(state_->cctx);
            state_->cctx = nullptr;
        }
        return info.Env().Undefined();
    }
};

Napi::FunctionReference ZstdCompressor::constructor;

class ZstdDecompressor : public Napi::ObjectWrap<ZstdDecompressor> {
public:
    static Napi::FunctionReference constructor;

    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "ZstdDecompressor", {
            InstanceMethod("transform", &ZstdDecompressor::Decompress),
            InstanceMethod("flush", &ZstdDecompressor::Finish),
            InstanceMethod("destroy", &ZstdDecompressor::Close)
        });
        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();
        exports.Set("ZstdDecompressor", func);
        return exports;
    }

    explicit ZstdDecompressor(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<ZstdDecompressor>(info),
          state_(std::make_shared<CodecState>(CodecState::Type::Decompress)) {
        state_->dctx = ZSTD_createDCtx();
        if (!state_->dctx) {
            state_->closed = true;
            Napi::Error::New(info.Env(), "Failed to create Zstd decompression context").ThrowAsJavaScriptException();
        }
    }

    ~ZstdDecompressor() = default;

private:
    std::shared_ptr<CodecState> state_;

    Napi::Value Decompress(const Napi::CallbackInfo& info) {
        std::vector<char> input;
        if (info.Length() < 2 || !info[0].IsBuffer()) {
            Napi::TypeError::New(info.Env(), "Usage: decompress(buffer, callback)").ThrowAsJavaScriptException();
            return info.Env().Undefined();
        }
        if (!CopyInputBuffer(info, 0, input)) return info.Env().Undefined();
        QueueOperation(info, state_, Operation::Decompress, 1, std::move(input));
        return info.Env().Undefined();
    }

    Napi::Value Finish(const Napi::CallbackInfo& info) {
        QueueOperation(info, state_, Operation::Finish, 0);
        return info.Env().Undefined();
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        if (state_->busy.load()) {
            Napi::Error::New(info.Env(), "Cannot close codec while an operation is running").ThrowAsJavaScriptException();
            return info.Env().Undefined();
        }
        state_->closed = true;
        if (state_->dctx) {
            ZSTD_freeDCtx(state_->dctx);
            state_->dctx = nullptr;
        }
        return info.Env().Undefined();
    }
};

Napi::FunctionReference ZstdDecompressor::constructor;

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    ZstdCompressor::Init(env, exports);
    ZstdDecompressor::Init(env, exports);
    return exports;
}

NODE_API_MODULE(zstd, Init)
