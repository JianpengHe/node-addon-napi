#include <napi.h>
#include <Windows.h>
// 辅助函数：将 UTF-8 转换为 UTF-16
std::wstring Utf8ToUtf16(const std::string& utf8Str) {
    if (utf8Str.empty()) {
        return std::wstring();
    }
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, NULL, 0);
    std::vector<wchar_t> utf16Str(sizeNeeded);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, &utf16Str[0], sizeNeeded);
    return std::wstring(utf16Str.begin(), utf16Str.end() - 1); // Exclude null terminator
}

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
    // 1. 进行编码转换
    std::wstring filePathUtf16 = Utf8ToUtf16(fileName);
    double ctime = info[1].As<Napi::Number>().DoubleValue();
    double mtime = info.Length() > 2 ? info[2].As<Napi::Number>().DoubleValue() : ctime;
    double atime = info.Length() > 3 ? info[3].As<Napi::Number>().DoubleValue() : mtime;

    HANDLE hFile = CreateFileW(filePathUtf16.c_str(),
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
