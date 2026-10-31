#include "version_proxy.h"

#include <mutex>
#include <string>

namespace {

HMODULE g_realVersion = nullptr;
std::once_flag g_loadOnce;

FARPROC Resolve(const char* name) {
    arkhan::InitializeRealVersion();
    return g_realVersion ? GetProcAddress(g_realVersion, name) : nullptr;
}

template <typename T>
T ResolveTyped(const char* name) {
    return reinterpret_cast<T>(Resolve(name));
}

} // namespace

namespace arkhan {

bool InitializeRealVersion() {
    std::call_once(g_loadOnce, [] {
        wchar_t systemDirectory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return;
        std::wstring path(systemDirectory, length);
        path += L"\\version.dll";
        g_realVersion = LoadLibraryW(path.c_str());
    });
    return g_realVersion != nullptr;
}

} // namespace arkhan

extern "C" {

BOOL WINAPI ArkhanGetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoA");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI ArkhanGetFileVersionInfoByHandle(DWORD a, HANDLE b, LPVOID* c, PDWORD d) {
    using Fn = BOOL(WINAPI*)(DWORD, HANDLE, LPVOID*, PDWORD);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoByHandle");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI ArkhanGetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoW");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI ArkhanGetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoExA");
    return fn ? fn(a, b, c, d, e) : FALSE;
}

BOOL WINAPI ArkhanGetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoExW");
    return fn ? fn(a, b, c, d, e) : FALSE;
}

DWORD WINAPI ArkhanGetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoSizeA");
    return fn ? fn(a, b) : 0;
}

DWORD WINAPI ArkhanGetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoSizeW");
    return fn ? fn(a, b) : 0;
}

DWORD WINAPI ArkhanGetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoSizeExA");
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI ArkhanGetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    const auto fn = ResolveTyped<Fn>("GetFileVersionInfoSizeExW");
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI ArkhanVerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPSTR e, PUINT f, LPSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    const auto fn = ResolveTyped<Fn>("VerFindFileA");
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI ArkhanVerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPWSTR e, PUINT f, LPWSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    const auto fn = ResolveTyped<Fn>("VerFindFileW");
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI ArkhanVerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d, LPCSTR e, LPCSTR f, LPSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    const auto fn = ResolveTyped<Fn>("VerInstallFileA");
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI ArkhanVerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    const auto fn = ResolveTyped<Fn>("VerInstallFileW");
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI ArkhanVerLanguageNameA(DWORD a, LPSTR b, DWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    const auto fn = ResolveTyped<Fn>("VerLanguageNameA");
    return fn ? fn(a, b, c) : 0;
}

DWORD WINAPI ArkhanVerLanguageNameW(DWORD a, LPWSTR b, DWORD c) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    const auto fn = ResolveTyped<Fn>("VerLanguageNameW");
    return fn ? fn(a, b, c) : 0;
}

BOOL WINAPI ArkhanVerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    const auto fn = ResolveTyped<Fn>("VerQueryValueA");
    return fn ? fn(a, b, c, d) : FALSE;
}

BOOL WINAPI ArkhanVerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    const auto fn = ResolveTyped<Fn>("VerQueryValueW");
    return fn ? fn(a, b, c, d) : FALSE;
}

} // extern "C"
