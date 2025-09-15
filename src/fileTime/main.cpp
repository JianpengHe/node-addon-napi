#include <napi.h>
#include <Windows.h>

inline void UnixTimeMsToFileTime(double ms, FILETIME* ft) {
    constexpr long long EPOCH_DIFF_SEC = 11644473600LL;
    long long ticks = static_cast<long long>((ms + static_cast<double>(EPOCH_DIFF_SEC) * 1000.0) * 10000.0);
    ft->dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFF);
    ft->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}

Napi::Value FileTime(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "Need at least file name and ctime").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string fileName = info[0].As<Napi::String>().Utf8Value();
    double ctime = info[1].As<Napi::Number>().DoubleValue();
    double mtime = info.Length() > 2 ? info[2].As<Napi::Number>().DoubleValue() : ctime;
    double atime = info.Length() > 3 ? info[3].As<Napi::Number>().DoubleValue() : mtime;

    HANDLE hFile = CreateFileA(fileName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        Napi::Error::New(env, "Get file handle failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    FILETIME c, a, m;
    UnixTimeMsToFileTime(ctime, &c);
    UnixTimeMsToFileTime(atime, &a);
    UnixTimeMsToFileTime(mtime, &m);

    int result = SetFileTime(hFile, &c, &a, &m) ? 0 : -1;
    CloseHandle(hFile);
    return Napi::Number::New(env, result);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("fileTime", Napi::Function::New(env, FileTime));
    return exports;
}

NODE_API_MODULE(addon, Init)
