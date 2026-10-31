#include "arkhan_runtime.h"

#include "arkhan_log.h"
#include "native_overlay_bridge.h"

#include <TlHelp32.h>
#include <algorithm>
#include <chrono>
#include <cstring>
// ARKHAN_NATIVE_AES_GCM_V3_INCLUDES_BEGIN
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
// ARKHAN_NATIVE_AES_GCM_V3_INCLUDES_END
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

namespace arkhan {

namespace {

constexpr DWORD kStartupPollMs = 250;
constexpr ULONGLONG kStartupGuardMs = 5000;
constexpr ULONGLONG kStartupCheckIntervalMs = 1000;
constexpr ULONGLONG kStartupResolveRetryMs = 1500;
constexpr ULONGLONG kLuaActivationGraceMs = 2000;

std::string LuaStringLiteral(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) >= 0x20) out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::filesystem::path ModuleDirectory(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

template <typename T>
T MethodPointer(const il2cpp::MethodInfo* method) {
    return method ? reinterpret_cast<T>(method->methodPointer) : nullptr;
}

bool ExecutablePointer(const void* pointer) {
    if (!pointer) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info))) return false;
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & executable) != 0;
}

void* SafeReadPointer(void* base, int32_t offset) {
    __try {
        return *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(base) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

using GetLuaManagerFn = void*(__fastcall*)(const il2cpp::MethodInfo*);

void* SafeCallLuaManagerGetter(GetLuaManagerFn getter) {
    __try {
        return getter ? getter(nullptr) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool SafeResolveCommonUpdateSlot(void* instance, std::uint16_t slotIndex,
                                 il2cpp::VirtualInvokeData** output) {
    __try {
        if (!instance || !output) return false;
        auto* klass = *reinterpret_cast<il2cpp::Il2CppClass**>(instance);
        if (!klass || klass->part2.vtableCount <= slotIndex) return false;
        *output = &klass->vtable[slotIndex];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

il2cpp::Il2CppClass* SafeReadClass(void* instance) {
    __try {
        return instance ? *reinterpret_cast<il2cpp::Il2CppClass**>(instance) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool SafeMethodInfoMatches(const il2cpp::MethodInfo* method, const void* expectedMethod,
                           std::uint16_t expectedSlot) {
    __try {
        if (!method || !method->name || method->slot != expectedSlot) return false;
        const auto direct = reinterpret_cast<const void*>(method->methodPointer);
        const auto virtualPointer = reinterpret_cast<const void*>(method->virtualMethodPointer);
        if (direct != expectedMethod && virtualPointer != expectedMethod) return false;
        constexpr char expectedName[] = "CommonUpdate";
        for (std::size_t index = 0; index < sizeof(expectedName); ++index) {
            if (method->name[index] != expectedName[index]) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeScanCommonUpdateSlot(void* instance, const void* expectedMethod,
                              std::uint16_t expectedSlot,
                              il2cpp::VirtualInvokeData** output,
                              std::uintptr_t* classOffset,
                              std::size_t* rawMatches,
                              std::size_t* validatedMatches) {
    auto* klass = SafeReadClass(instance);
    if (!klass || !output || !classOffset || !rawMatches || !validatedMatches) return false;
    *rawMatches = 0;
    *validatedMatches = 0;

    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(klass, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;

    const auto start = reinterpret_cast<std::uintptr_t>(klass);
    const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (regionEnd <= start) return false;
    const auto available = static_cast<std::size_t>(regionEnd - start);
    const auto scanBytes = std::min<std::size_t>(available, 0x1000);

    il2cpp::VirtualInvokeData* found = nullptr;
    std::uintptr_t foundOffset = 0;
    std::size_t matches = 0;
    for (std::size_t offset = 0; offset + sizeof(il2cpp::VirtualInvokeData) <= scanBytes;
         offset += alignof(void*)) {
        auto* candidate = reinterpret_cast<il2cpp::VirtualInvokeData*>(start + offset);
        il2cpp::Il2CppMethodPointer methodPointer = nullptr;
        const il2cpp::MethodInfo* method = nullptr;
        __try {
            methodPointer = candidate->methodPtr;
            method = candidate->method;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (reinterpret_cast<const void*>(methodPointer) != expectedMethod) continue;
        ++*rawMatches;
        if (!SafeMethodInfoMatches(method, expectedMethod, expectedSlot)) continue;
        found = candidate;
        foundOffset = offset;
        ++matches;
    }

    *validatedMatches = matches;

    if (matches != 1 || !found) return false;
    *output = found;
    *classOffset = foundOffset;
    return true;
}

bool SafeSlotPointsTo(const il2cpp::VirtualInvokeData* slot, const void* expectedMethod) {
    __try {
        return slot && reinterpret_cast<const void*>(slot->methodPtr) == expectedMethod;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// ARKHAN_NATIVE_AES_GCM_V3_HELPER_BEGIN
std::string ArkhanDecryptBootstrapV3(const std::string& package) {
    constexpr char magic[] = "ARK-PKG-1";
    constexpr std::size_t magicBytes = sizeof(magic) - 1;
    constexpr std::size_t nonceBytes = 12;
    constexpr std::size_t tagBytes = 16;
    if (package.size() <= magicBytes + nonceBytes + tagBytes ||
        std::memcmp(package.data(), magic, magicBytes) != 0) return {};
    const auto cipherSize = package.size() - magicBytes - nonceBytes - tagBytes;
    if (cipherSize > static_cast<std::size_t>(std::numeric_limits<ULONG>::max())) return {};
    const unsigned char first[32] = { 0xcb, 0xff, 0x60, 0x74, 0xb3, 0x4e, 0xee, 0x19, 0xbd, 0x61, 0x29, 0x64, 0x4e, 0x42, 0x31, 0x8d, 0x98, 0x11, 0x8e, 0xda, 0x6e, 0xad, 0x97, 0xaf, 0x4c, 0xbd, 0x86, 0x25, 0x8f, 0xee, 0x7d, 0x83 };
    const unsigned char second[32] = { 0x70, 0x84, 0xcd, 0xa7, 0x5f, 0x03, 0x44, 0xa4, 0xfc, 0x32, 0x6c, 0x5f, 0x90, 0x77, 0xb4, 0xc2, 0x22, 0xf3, 0x81, 0x1e, 0x2d, 0x3a, 0xb1, 0x76, 0xc6, 0x1b, 0xd7, 0xc0, 0xcd, 0x43, 0x47, 0x86 };
    UCHAR keyBytes[32] = {};
    for (std::size_t i = 0; i < sizeof(keyBytes); ++i)
        keyBytes[i] = static_cast<UCHAR>(first[i] ^ second[i]);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    std::vector<UCHAR> keyObject;
    std::string plaintext;
    do {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) break;
        if (!BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) break;
        DWORD objectBytes = 0;
        DWORD cbResult = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &cbResult, 0))) break;
        keyObject.resize(objectBytes);
        if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &hkey,
                keyObject.data(), static_cast<ULONG>(keyObject.size()), keyBytes, sizeof(keyBytes), 0))) break;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
        BCRYPT_INIT_AUTH_MODE_INFO(auth);
        auth.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(package.data() + magicBytes));
        auth.cbNonce = static_cast<ULONG>(nonceBytes);
        auth.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(package.data() + magicBytes + nonceBytes));
        auth.cbTag = static_cast<ULONG>(tagBytes);
        auth.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(package.data()));
        auth.cbAuthData = static_cast<ULONG>(magicBytes);
        plaintext.resize(cipherSize);
        ULONG decryptedBytes = 0;
        if (!BCRYPT_SUCCESS(BCryptDecrypt(hkey,
                reinterpret_cast<PUCHAR>(const_cast<char*>(package.data() + magicBytes + nonceBytes + tagBytes)),
                static_cast<ULONG>(cipherSize), &auth, nullptr, 0,
                reinterpret_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
                &decryptedBytes, 0)) || decryptedBytes != plaintext.size()) {
            plaintext.clear();
        }
    } while (false);
    SecureZeroMemory(keyBytes, sizeof(keyBytes));
    if (!keyObject.empty()) SecureZeroMemory(keyObject.data(), keyObject.size());
    if (hkey) BCryptDestroyKey(hkey);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return plaintext;
}
// ARKHAN_NATIVE_AES_GCM_V3_HELPER_END

std::string NarrowModuleName(const wchar_t* value) {
    if (!value || !*value) return {};
    const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                               nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    const auto written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                              result.data(), required, nullptr, nullptr);
    if (written != required) return {};
    result.pop_back();
    return result;
}

using NativeGetTop = int(__cdecl*)(void*);
using NativeSetTop = void(__cdecl*)(void*, int);
using NativeLoadBuffer = int(__cdecl*)(void*, const char*, int, const char*);
using NativePCall = int(__cdecl*)(void*, int, int, int);
using NativeToLString = const char*(__cdecl*)(void*, int, std::size_t*);

struct NativeLuaSurfaceCandidate {
    HMODULE module{};
    std::string name;
    NativeGetTop getTop{};
    NativeSetTop setTop{};
    NativeLoadBuffer loadBuffer{};
    NativePCall pcall{};
    NativeToLString toLString{};
};

bool DiscoverNativeLuaSurface(NativeLuaSurfaceCandidate* output, std::size_t* completeMatches) {
    if (output) *output = {};
    if (completeMatches) *completeMatches = 0;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                                     GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    NativeLuaSurfaceCandidate lastMatch{};
    std::size_t matches = 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const auto getTop = reinterpret_cast<NativeGetTop>(GetProcAddress(entry.hModule, "lua_gettop"));
            const auto setTop = reinterpret_cast<NativeSetTop>(GetProcAddress(entry.hModule, "lua_settop"));
            const auto loadBuffer = reinterpret_cast<NativeLoadBuffer>(GetProcAddress(entry.hModule, "xluaL_loadbuffer"));
            const auto pcall = reinterpret_cast<NativePCall>(GetProcAddress(entry.hModule, "lua_pcall"));
            const auto toLString = reinterpret_cast<NativeToLString>(GetProcAddress(entry.hModule, "lua_tolstring"));
            if (!getTop || !setTop || !loadBuffer || !pcall || !toLString) continue;
            if (!ExecutablePointer(reinterpret_cast<void*>(getTop)) ||
                !ExecutablePointer(reinterpret_cast<void*>(setTop)) ||
                !ExecutablePointer(reinterpret_cast<void*>(loadBuffer)) ||
                !ExecutablePointer(reinterpret_cast<void*>(pcall)) ||
                !ExecutablePointer(reinterpret_cast<void*>(toLString))) continue;

            ++matches;
            lastMatch.module = entry.hModule;
            lastMatch.name = NarrowModuleName(entry.szModule);
            lastMatch.getTop = getTop;
            lastMatch.setTop = setTop;
            lastMatch.loadBuffer = loadBuffer;
            lastMatch.pcall = pcall;
            lastMatch.toLString = toLString;
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (completeMatches) *completeMatches = matches;
    if (output && matches != 0) *output = lastMatch;
    return matches != 0;
}

bool SafeNativeStackProbe(NativeGetTop getTop, NativeSetTop setTop, void* state,
                          int* before, int* after) {
    __try {
        if (!getTop || !setTop || !state || !before || !after) return false;
        *before = getTop(state);
        setTop(state, *before);
        *after = getTop(state);
        return *before >= 0 && *after == *before;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeNativeGetTop(NativeGetTop function, void* state, int* top) {
    __try {
        if (!function || !state || !top) return false;
        *top = function(state);
        return *top >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeNativeSetTop(NativeSetTop function, void* state, int top) {
    __try {
        if (!function || !state) return false;
        function(state, top);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeNativeLoadBuffer(NativeLoadBuffer function, void* state, const char* buffer,
                          int size, const char* name, int* result) {
    __try {
        if (!function || !state || !buffer || !name || !result) return false;
        *result = function(state, buffer, size, name);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeNativePCall(NativePCall function, void* state, int argumentCount,
                     int resultCount, int errorFunction, int* result) {
    __try {
        if (!function || !state || !result) return false;
        *result = function(state, argumentCount, resultCount, errorFunction);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeNativeToString(NativeToLString function, void* state, int index,
                        const char** value, std::size_t* length) {
    __try {
        if (!function || !state || !value || !length) return false;
        *length = 0;
        *value = function(state, index, length);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopyBytes(const char* source, std::size_t length, char* destination,
                   std::size_t capacity, std::size_t* copied) {
    __try {
        if (!destination || !copied || (!source && length != 0)) return false;
        const auto count = std::min(length, capacity);
        if (count != 0) std::memcpy(destination, source, count);
        *copied = count;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

Runtime& Runtime::Instance() {
    static Runtime instance;
    return instance;
}

bool Runtime::Start(HMODULE selfModule) {
    if (bootstrapThread_) return true;
    selfModule_ = selfModule;
    root_ = ModuleDirectory(selfModule_) / L"Arkhan";
    Log::Instance().Initialize(root_);
    proxyReady_.store(true);
    Log::Instance().Write("host", "ARKHAN_PROXY_READY version=0.3.0-r25 host=VERSION.dll");

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        Log::Instance().Write("host", "ARKHAN_START_FAILED reason=CreateEvent");
        return false;
    }

    bootstrapThread_ = CreateThread(nullptr, 0, &Runtime::BootstrapThreadEntry, this, 0, nullptr);
    if (!bootstrapThread_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        Log::Instance().Write("host", "ARKHAN_START_FAILED reason=CreateThread");
        return false;
    }
    return true;
}

void Runtime::Stop() {
    if (stopping_.exchange(true)) return;
    if (stopEvent_) SetEvent(stopEvent_);
}

DWORD WINAPI Runtime::BootstrapThreadEntry(void* context) {
    static_cast<Runtime*>(context)->Bootstrap();
    return 0;
}

void Runtime::Bootstrap() {
    LoadConfiguration();

    const auto setStartupPhase = [this](const char* phase) {
        std::lock_guard lock(stateMutex_);
        startupPhase_ = phase ? phase : "unknown";
    };

    setStartupPhase("waiting-gameassembly");
    startupGuardRemainingMs_.store(kStartupGuardMs);
    Log::Instance().Write("host", "ARKHAN_WAIT_GAMEASSEMBLY");
    Log::Instance().Write("host", "ARKHAN_STARTUP_GUARD_WAIT gameAssembly=0 unityPlayer=0 nativeLuaSurface=0");

    ULONGLONG guardArmedAtMs = 0;
    ULONGLONG nextSurfaceCheckMs = 0;
    std::size_t surfaceMatches = 0;
    NativeLuaSurfaceCandidate surfaceCandidate{};

    while (!stopping_.load() && WaitForSingleObject(stopEvent_, kStartupPollMs) == WAIT_TIMEOUT) {
        const auto now = GetTickCount64();
        const bool gameAssemblyLoaded = GetModuleHandleW(L"GameAssembly.dll") != nullptr;
        const bool unityPlayerLoaded = GetModuleHandleW(L"UnityPlayer.dll") != nullptr;

        if (now >= nextSurfaceCheckMs) {
            surfaceMatches = 0;
            surfaceCandidate = {};
            DiscoverNativeLuaSurface(&surfaceCandidate, &surfaceMatches);
            nextSurfaceCheckMs = now + kStartupCheckIntervalMs;
        }
        const bool nativeLuaSurfaceLoaded = surfaceMatches != 0;

        if (!gameAssemblyLoaded || !unityPlayerLoaded || !nativeLuaSurfaceLoaded) {
            guardArmedAtMs = 0;
            startupGuardRemainingMs_.store(kStartupGuardMs);
            if (!gameAssemblyLoaded) setStartupPhase("waiting-gameassembly");
            else if (!unityPlayerLoaded) setStartupPhase("waiting-unityplayer");
            else setStartupPhase("waiting-native-lua-surface");
            continue;
        }

        if (guardArmedAtMs == 0) {
            guardArmedAtMs = now;
            setStartupPhase("startup-grace");
            std::ostringstream armed;
            armed << "ARKHAN_STARTUP_GUARD_ARMED graceMs=" << kStartupGuardMs
                  << " nativeLuaMatches=" << surfaceMatches;
            if (!surfaceCandidate.name.empty()) armed << " module=" << surfaceCandidate.name;
            Log::Instance().Write("host", armed.str());
        }

        const auto elapsed = now - guardArmedAtMs;
        if (elapsed < kStartupGuardMs) {
            startupGuardRemainingMs_.store(kStartupGuardMs - elapsed);
            continue;
        }

        startupGuardRemainingMs_.store(0);
        setStartupPhase("startup-guard-released");
        Log::Instance().Write("host", "ARKHAN_STARTUP_GUARD_RELEASED action=enable-runtime-resolver");
        break;
    }

    if (!stopping_.load()) {
        LoadBootstrapScript();
    }

    ULONGLONG nextResolveAttemptMs = 0;
    while (!stopping_.load() && WaitForSingleObject(stopEvent_, 100) == WAIT_TIMEOUT) {
        const auto now = GetTickCount64();
        if (now < nextResolveAttemptMs) continue;

        const auto attempt = startupResolveAttempts_.fetch_add(1) + 1;
        setStartupPhase("runtime-resolve-call");
        if (attempt == 1 || attempt % 5 == 0) {
            Log::Instance().Write("host", "ARKHAN_STARTUP_RESOLVE_ATTEMPT count=" + std::to_string(attempt));
        }

        if (ResolveAndInstall()) {
            setStartupPhase("dispatcher-ready");
            Log::Instance().Write("host", "ARKHAN_STARTUP_RESOLVE_READY attempts=" + std::to_string(attempt));
            break;
        }

        setStartupPhase("runtime-resolve-backoff");
        nextResolveAttemptMs = now + kStartupResolveRetryMs;
    }

    std::uint32_t nativeSurfaceRetryTicks = 0;
    std::uint32_t overlayAttempts = 0;
    bool overlayStarted = false;
    ULONGLONG overlayEligibleSinceMs = 0;
    ULONGLONG nextOverlayAttemptMs = 0;
    while (!stopping_.load() && WaitForSingleObject(stopEvent_, 500) == WAIT_TIMEOUT) {
        if (exportsUnavailable_.load() && !nativeLuaSurfaceReady_.load(std::memory_order_acquire) &&
            ++nativeSurfaceRetryTicks % 4 == 0) {
            ResolveNativeLuaSurface();
        }

        if (nativeOverlayEnabled_.load() && !overlayStarted) {
            const bool runtimeAlive = dispatcherReady_.load() && luaReady_.load() &&
                                      luaExecutionReady_.load() && bootstrapReady_.load();
            const auto now = GetTickCount64();
            if (!runtimeAlive) {
                overlayEligibleSinceMs = 0;
            } else {
                if (overlayEligibleSinceMs == 0) overlayEligibleSinceMs = now;
                if (now - overlayEligibleSinceMs >= 3000 && now >= nextOverlayAttemptMs &&
                    overlayAttempts < 5) {
                    ++overlayAttempts;
                    if (NativeOverlayBridge::Instance().Start(root_)) {
                        overlayStarted = true;
                        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_DEFERRED_START_OK graceMs=3000");
                    } else {
                        nextOverlayAttemptMs = now + 3000;
                        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_DEFERRED_RETRY attempt=" +
                                                             std::to_string(overlayAttempts));
                    }
                }
            }
        }
    }

    NativeOverlayBridge::Instance().Stop();
    if (patchedSlot_ && originalCommonUpdate_) {
        DWORD oldProtect = 0;
        if (VirtualProtect(patchedSlot_, sizeof(*patchedSlot_), PAGE_READWRITE, &oldProtect)) {
            InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&patchedSlot_->methodPtr),
                                       reinterpret_cast<void*>(originalCommonUpdate_));
            DWORD ignored = 0;
            VirtualProtect(patchedSlot_, sizeof(*patchedSlot_), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), patchedSlot_, sizeof(*patchedSlot_));
            Log::Instance().Write("runtime", "ARKHAN_DISPATCH_RESTORED method=LuaManager.CommonUpdate");
        }
        patchedSlot_ = nullptr;
        dispatcherReady_.store(false);
    }
    api_.DetachThread(attachedThread_);
    attachedThread_ = nullptr;
}

bool Runtime::ResolveAndInstall() {
    const auto gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!gameAssembly) return false;

    if (!api_.Ready() && !exportsUnavailable_.load()) {
        std::string missingExports;
        if (!api_.Initialize(gameAssembly, &missingExports)) {
            const auto error = "missing-il2cpp-export:" + missingExports;
            {
                std::lock_guard lock(stateMutex_);
                lastError_ = error;
                discoveryMode_ = "exports-unavailable";
            }
            exportsUnavailable_.store(true);
            Log::Instance().Write("il2cpp", "ARKHAN_IL2CPP_EXPORT_MISSING names=" + missingExports);
        } else {
            attachedThread_ = api_.AttachCurrentThread();
            if (!attachedThread_) {
                std::lock_guard lock(stateMutex_);
                lastError_ = "il2cpp-thread-attach-failed";
                return false;
            }
            il2cppReady_.store(true);
            Log::Instance().Write("il2cpp", "ARKHAN_IL2CPP_READY mode=metadata-exports thread=attached");
        }
    }

    if (exportsUnavailable_.load()) return ResolveKnownRvaAndInstall(gameAssembly);

    luaManagerClass_ = api_.FindClass("FunPlus.WorldX.Manager", "LuaManager");
    luaEnvClass_ = api_.FindClass("XLua", "LuaEnv");
    luaApiClass_ = api_.FindClass("XLua.LuaDLL", "Lua");
    if (!luaManagerClass_ || !luaEnvClass_ || !luaApiClass_) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "lua-class-not-ready";
        return false;
    }

    luaEnvOffset_ = api_.FindFieldOffset(luaManagerClass_, "luaenv");
    rawLuaOffset_ = api_.FindFieldOffset(luaEnvClass_, "rawL");
    commonUpdateMethod_ = api_.FindMethod(luaManagerClass_, "CommonUpdate", 0);
    luaGetTopMethod_ = api_.FindMethod(luaApiClass_, "lua_gettop", 1);
    luaSetTopMethod_ = api_.FindMethod(luaApiClass_, "lua_settop", 2);
    luaLoadBufferMethod_ = api_.FindMethod(luaApiClass_, "luaL_loadbuffer", 3);
    luaPCallMethod_ = api_.FindMethod(luaApiClass_, "lua_pcall", 4);
    luaToStringMethod_ = api_.FindMethod(luaApiClass_, "lua_tostring", 2);

    if (luaEnvOffset_ < 0 || rawLuaOffset_ < 0 || !commonUpdateMethod_ ||
        !luaGetTopMethod_ || !luaSetTopMethod_ || !luaLoadBufferMethod_ ||
        !luaPCallMethod_ || !luaToStringMethod_) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "lua-surface-incomplete";
        return false;
    }

    luaGetTopFn_ = MethodPointer<LuaGetTopFn>(luaGetTopMethod_);
    luaSetTopFn_ = MethodPointer<LuaSetTopFn>(luaSetTopMethod_);
    luaLoadBufferFn_ = MethodPointer<LuaLoadBufferFn>(luaLoadBufferMethod_);
    luaPCallFn_ = MethodPointer<LuaPCallFn>(luaPCallMethod_);
    luaToStringFn_ = MethodPointer<LuaToStringFn>(luaToStringMethod_);

    if (commonUpdateMethod_->slot == UINT16_MAX ||
        commonUpdateMethod_->slot >= luaManagerClass_->part2.vtableCount) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "common-update-slot-invalid";
        return false;
    }

    auto* slot = &luaManagerClass_->vtable[commonUpdateMethod_->slot];
    if (!ExecutablePointer(reinterpret_cast<void*>(slot->methodPtr))) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "common-update-pointer-invalid";
        return false;
    }

    originalCommonUpdate_ = reinterpret_cast<CommonUpdateFn>(slot->methodPtr);
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "common-update-vtable-protect-failed";
        return false;
    }
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slot->methodPtr),
                               reinterpret_cast<void*>(&Runtime::CommonUpdateHook));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    patchedSlot_ = slot;
    dispatcherReady_.store(true);
    luaExecutionReady_.store(true);

    {
        std::lock_guard lock(stateMutex_);
        lastError_.clear();
        discoveryMode_ = "metadata-exports";
        luaBridgeMode_ = "managed-wrappers";
    }
    std::ostringstream details;
    details << "ARKHAN_DISPATCH_READY method=LuaManager.CommonUpdate slot=" << commonUpdateMethod_->slot
            << " luaenvOffset=0x" << std::hex << luaEnvOffset_
            << " rawLOffset=0x" << rawLuaOffset_;
    Log::Instance().Write("runtime", details.str());
    return true;
}

bool Runtime::ResolveKnownRvaAndInstall(HMODULE gameAssembly) {
        constexpr std::uintptr_t getLuaManagerRva = 0x74FA80;
    constexpr std::uintptr_t commonUpdateRva = 0x172AD60;
    constexpr std::uint16_t commonUpdateSlot = 4;
    constexpr int32_t luaEnvOffset = 0x18;
    constexpr int32_t rawLuaOffset = 0x10;

    const auto base = reinterpret_cast<std::uintptr_t>(gameAssembly);
    const auto getter = reinterpret_cast<GetLuaManagerFn>(base + getLuaManagerRva);
    const auto expectedCommonUpdate = reinterpret_cast<void*>(base + commonUpdateRva);
    if (!ExecutablePointer(reinterpret_cast<void*>(getter)) || !ExecutablePointer(expectedCommonUpdate)) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "known-rva-pointer-not-executable";
        discoveryMode_ = "known-rva";
        return false;
    }

    void* luaManager = SafeCallLuaManagerGetter(getter);
    if (!luaManager) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "known-rva-waiting-lua-manager";
        discoveryMode_ = "known-rva";
        return false;
    }

    il2cpp::VirtualInvokeData* slot = nullptr;
    std::uintptr_t classSlotOffset = 0;
    std::size_t rawSlotMatches = 0;
    std::size_t validatedSlotMatches = 0;
    std::string slotResolver = "layout";
    const auto layoutResolved = SafeResolveCommonUpdateSlot(luaManager, commonUpdateSlot, &slot) &&
                                SafeSlotPointsTo(slot, expectedCommonUpdate);
    if (layoutResolved) {
        const auto klass = SafeReadClass(luaManager);
        classSlotOffset = klass ? reinterpret_cast<std::uintptr_t>(slot) - reinterpret_cast<std::uintptr_t>(klass) : 0;
    } else {
        slot = nullptr;
        slotResolver = "validated-scan";
        if (!SafeScanCommonUpdateSlot(luaManager, expectedCommonUpdate, commonUpdateSlot,
                                      &slot, &classSlotOffset, &rawSlotMatches, &validatedSlotMatches)) {
            const auto klass = SafeReadClass(luaManager);
            std::ostringstream mismatch;
            mismatch << "known-rva-common-update-not-found:manager=0x" << std::hex
                     << reinterpret_cast<std::uintptr_t>(luaManager)
                     << ",klass=0x" << reinterpret_cast<std::uintptr_t>(klass)
                     << ",expected=0x" << reinterpret_cast<std::uintptr_t>(expectedCommonUpdate)
                     << ",rawMatches=" << std::dec << rawSlotMatches
                     << ",validatedMatches=" << validatedSlotMatches;
            std::lock_guard lock(stateMutex_);
            lastError_ = mismatch.str();
            discoveryMode_ = "known-rva";
            return false;
        }
    }
    if (!SafeSlotPointsTo(slot, expectedCommonUpdate)) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "known-rva-slot-changed-before-patch";
        discoveryMode_ = "known-rva";
        return false;
    }

    originalCommonUpdate_ = reinterpret_cast<CommonUpdateFn>(slot->methodPtr);
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
        std::lock_guard lock(stateMutex_);
        lastError_ = "known-rva-vtable-protect-failed";
        discoveryMode_ = "known-rva";
        return false;
    }
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slot->methodPtr),
                               reinterpret_cast<void*>(&Runtime::CommonUpdateHook));
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

    luaEnvOffset_ = luaEnvOffset;
    rawLuaOffset_ = rawLuaOffset;
    patchedSlot_ = slot;
    il2cppReady_.store(true);
    dispatcherReady_.store(true);
    luaExecutionReady_.store(false);
    {
        std::lock_guard lock(stateMutex_);
        lastError_.clear();
        discoveryMode_ = "known-rva";
        luaBridgeMode_ = "native-exports-unresolved";
    }

    std::ostringstream ready;
    ready << "ARKHAN_IL2CPP_READY mode=known-rva base=0x" << std::hex << base
          << " getterRva=0x" << getLuaManagerRva
          << " commonUpdateRva=0x" << commonUpdateRva
          << " classSlotOffset=0x" << classSlotOffset
          << " slotResolver=" << slotResolver;
    Log::Instance().Write("il2cpp", ready.str());
    Log::Instance().Write("runtime", "ARKHAN_DISPATCH_READY mode=known-rva method=LuaManager.CommonUpdate slot=4 luaenvOffset=0x18 rawLOffset=0x10");
    ResolveNativeLuaSurface();
    if (!nativeLuaSurfaceReady_.load(std::memory_order_acquire)) {
        Log::Instance().Write("lua", "ARKHAN_LUA_EXECUTION_BLOCKED reason=native-xlua-surface-unresolved");
    }
    return true;
}

bool Runtime::ResolveNativeLuaSurface() {
    if (nativeLuaSurfaceReady_.load(std::memory_order_acquire)) return true;

    NativeLuaSurfaceCandidate candidate{};
    std::size_t completeMatches = 0;
    DiscoverNativeLuaSurface(&candidate, &completeMatches);

    if (completeMatches != 1 || !candidate.module) {
        std::lock_guard lock(stateMutex_);
        luaBridgeMode_ = completeMatches == 0 ? "native-exports-unresolved" : "native-exports-ambiguous";
        return false;
    }

    nativeLuaGetTopFn_ = candidate.getTop;
    nativeLuaSetTopFn_ = candidate.setTop;
    nativeLuaLoadBufferFn_ = candidate.loadBuffer;
    nativeLuaPCallFn_ = candidate.pcall;
    nativeLuaToLStringFn_ = candidate.toLString;
    {
        std::lock_guard lock(stateMutex_);
        nativeLuaModule_ = candidate.name;
        luaBridgeMode_ = "native-exports-awaiting-game-thread-validation";
    }
    nativeLuaSurfaceReady_.store(true, std::memory_order_release);

    std::ostringstream ready;
    ready << "ARKHAN_XLUA_NATIVE_SURFACE_READY module=" << candidate.name
          << " base=0x" << std::hex << reinterpret_cast<std::uintptr_t>(candidate.module)
          << " symbols=lua_gettop,lua_settop,xluaL_loadbuffer,lua_pcall,lua_tolstring";
    Log::Instance().Write("lua", ready.str());
    return true;
}

bool Runtime::ValidateNativeLuaSurface(void* state) {
    int before = -1;
    int after = -1;
    if (!nativeLuaSurfaceReady_.load(std::memory_order_acquire) ||
        !SafeNativeStackProbe(nativeLuaGetTopFn_, nativeLuaSetTopFn_, state, &before, &after)) {
        {
            std::lock_guard lock(stateMutex_);
            lastError_ = "native-xlua-stack-validation-failed";
            luaBridgeMode_ = "native-exports-validation-failed";
        }
        Log::Instance().Write("lua", "ARKHAN_XLUA_STACK_PROBE_FAILED action=execution-remains-disabled");
        return false;
    }

    luaExecutionReady_.store(true);
    {
        std::lock_guard lock(stateMutex_);
        lastError_.clear();
        luaBridgeMode_ = "native-exports";
    }
    Log::Instance().Write("lua", "ARKHAN_XLUA_STACK_PROBE_OK top=" + std::to_string(before) +
                                 " action=manual-execution-enabled");
    return true;
}

void __fastcall Runtime::CommonUpdateHook(void* self, const il2cpp::MethodInfo* method) {
    auto& runtime = Instance();
    if (runtime.originalCommonUpdate_) runtime.originalCommonUpdate_(self, method);
    const auto started = std::chrono::steady_clock::now();
    runtime.OnCommonUpdate(self);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    runtime.hookCalls_.fetch_add(1, std::memory_order_relaxed);
    auto maximum = runtime.maxHookOverheadUs_.load(std::memory_order_relaxed);
    const auto sample = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
    while (sample > maximum &&
           !runtime.maxHookOverheadUs_.compare_exchange_weak(maximum, sample,
                                                             std::memory_order_relaxed)) {
    }
}

void Runtime::OnCommonUpdate(void* luaManager) {
    if (!luaManager || luaEnvOffset_ < 0 || rawLuaOffset_ < 0) return;
    const auto nowMs = GetTickCount64();
    if (gameThreadId_.load() == 0) {
        gameThreadId_.store(GetCurrentThreadId());
        Log::Instance().Write("runtime", "ARKHAN_GAME_THREAD_CAPTURED tid=" + std::to_string(GetCurrentThreadId()));
    }

    void* luaEnv = SafeReadPointer(luaManager, luaEnvOffset_);
    void* state = luaEnv ? SafeReadPointer(luaEnv, rawLuaOffset_) : nullptr;

    const auto address = reinterpret_cast<std::uintptr_t>(state);
    const auto previous = luaState_.exchange(address);
    luaReady_.store(state != nullptr);
    if (address && address != previous) {
        std::ostringstream stream;
        stream << "ARKHAN_LUA_STATE_CAPTURED address=0x" << std::hex << address;
        Log::Instance().Write("lua", stream.str());
        bootstrapReady_.store(false);
        bootstrapAttempted_.store(false);
        nativeLuaProbeAttempted_.store(false);
        if (exportsUnavailable_.load()) luaExecutionReady_.store(false);
        luaStateStableSinceMs_.store(nowMs);
        lastNativeUiTickMs_ = 0;
        NativeOverlayBridge::Instance().ClearLuaSnapshot();
        Log::Instance().Write("lua", "ARKHAN_LUA_ACTIVATION_DELAY graceMs=" + std::to_string(kLuaActivationGraceMs));
    } else if (!address && previous) {
        Log::Instance().Write("lua", "ARKHAN_LUA_STATE_CLEARED");
        bootstrapReady_.store(false);
        bootstrapAttempted_.store(false);
        nativeLuaProbeAttempted_.store(false);
        if (exportsUnavailable_.load()) luaExecutionReady_.store(false);
        luaStateStableSinceMs_.store(0);
        lastNativeUiTickMs_ = 0;
        NativeOverlayBridge::Instance().ClearLuaSnapshot();
    }

    if (!state) return;

    const auto stableSinceMs = luaStateStableSinceMs_.load();
    if (stableSinceMs == 0 || nowMs - stableSinceMs < kLuaActivationGraceMs) return;

    if (exportsUnavailable_.load() && nativeLuaSurfaceReady_.load(std::memory_order_acquire) &&
        !luaExecutionReady_.load() && !nativeLuaProbeAttempted_.exchange(true)) {
        ValidateNativeLuaSurface(state);
    }

    bool expected = false;
    if (luaExecutionReady_.load() && autoBootstrap_.load() && !bootstrapReady_.load() && !bootstrapScript_.empty() &&
        bootstrapAttempted_.compare_exchange_strong(expected, true)) {
        const auto result = ExecuteLua(state, bootstrapScript_, "@Arkhan/Runtime/arkhan_bootstrap.lua");
        if (result.rfind("OK|", 0) == 0) {
            bootstrapReady_.store(true);
            {
                std::lock_guard lock(stateMutex_);
                startupPhase_ = "runtime-ready";
            }
            Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_RESULT " + result);
        } else {
            Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_FAILED " + result);
        }
    }

    for (int processed = 0; processed < 4; ++processed) {
        std::shared_ptr<LuaCommand> command;
        {
            std::lock_guard lock(stateMutex_);
            if (commands_.empty()) break;
            command = commands_.front();
            commands_.pop_front();
        }
        Complete(command, ExecuteLua(state, command->code, command->chunkName));
    }

    if (bootstrapReady_.load() && luaExecutionReady_.load() && NativeOverlayBridge::Instance().Loaded() &&
        (lastNativeUiTickMs_ == 0 || nowMs - lastNativeUiTickMs_ >= 100)) {
        lastNativeUiTickMs_ = nowMs;
        const auto controls = NativeOverlayBridge::Instance().ReadControls();
        const auto boolean = [](bool value) { return value ? "true" : "false"; };
        std::ostringstream script;
        script << "if type(__arkhan.mobVacuumModeFrame)=='function' then __arkhan.mobVacuumModeFrame("
               << boolean(controls.mobVacuumStun)
               << "); end; ";
        script << "if type(__arkhan.playerGodModeFrame)=='function' then __arkhan.playerGodModeFrame("
               << boolean(controls.playerGodMode) << "); end; ";
        script << "if type(__arkhan.playerOverloadReliefFrame)=='function' then __arkhan.playerOverloadReliefFrame("
               << boolean(controls.playerOverloadRelief) << "); end; ";
        script << "if type(__arkhan.mobVacuumFrame)=='function' then __arkhan.mobVacuumFrame("
               << boolean(controls.mobVacuumEnabled) << ','
               << controls.mobVacuumRadius << ','
               << controls.mobVacuumMaxMobs << ','
               << controls.mobVacuumDistance << ','
               << controls.mobVacuumRepositionTolerance
               << "); end; ";
        script << "if type(__arkhan.autofarmFrame)=='function' then __arkhan.autofarmFrame("
               << boolean(controls.autofarmEnabled) << ','
               << boolean(controls.autofarmIncludeChests) << ','
               << boolean(controls.autofarmIncludeAmber) << ','
               << controls.autofarmScanRadius << ','
               << controls.autofarmEmptyConfirmRadius << ','
               << controls.autofarmTeleportDelayMs << ','
               << controls.autofarmSettleDelayMs << ','
               << controls.autofarmResetRequest
               << "); end; ";
        script << "if type(__arkhan.autoquestFrame)=='function' then __arkhan.autoquestFrame("
               << boolean(controls.autoquestEnabled) << ','
               << controls.autoquestSelectedQuestId << ','
               << controls.autoquestTeleportRequest << ','
               << controls.autoquestSelectedTemplateId << ','
               << controls.autoquestSelectedMarkId << ','
               << controls.autoquestSelectedMarkRealSceneId
               << "); end; ";
        script << "return __arkhan.nativeUiFrame(" << controls.revision << ','
               << boolean(controls.infiniteStamina) << ','
               << boolean(controls.noFallDamage) << ','
               << boolean(controls.speedBoost) << ',' << controls.speedMultiplier << ','
               << boolean(controls.autoCollect) << ',' << controls.collectRange << ','
               << boolean(controls.autoChest) << ',' << controls.chestRange << ','
               << boolean(controls.captureAssistant) << ',' << boolean(controls.captureAutoThrow) << ','
               << boolean(controls.captureFastThrow) << ',' << boolean(controls.captureSkipCameraTransition) << ','
               << controls.captureThrowAnimSpeed << ',' << controls.captureBallSpeed << ',' << controls.captureRecoveryDelay << ','
               << controls.captureTargetMode << ',' << controls.captureRange << ',' << controls.captureMinPercent << ','
               << LuaStringLiteral(controls.captureSpecies) << ','
               << boolean(controls.autoDialogueSkip) << ',' << boolean(controls.autoCutsceneSkip) << ','
               << boolean(controls.fovOverride) << ',' << controls.fov << ','
               << boolean(controls.interconnectionGodMode) << ','
               << boolean(controls.interconnectionNoCooldown) << ','
               << boolean(controls.interconnectionNoCost) << ','
               << boolean(controls.interconnectionDamage) << ',' << controls.interconnectionDamageMultiplier << ','
               << boolean(controls.interconnectionAttackSpeed) << ',' << controls.interconnectionAttackSpeedMultiplier << ','
               << boolean(controls.interconnectionAttackFurious) << ','
               << boolean(controls.interconnectionAttackEnhance) << ','
               << boolean(controls.interconnectionAttackHunt) << ','
               << boolean(controls.espEnabled) << ',' << controls.espRange << ','
               << boolean(controls.espCollect) << ',' << boolean(controls.espChest) << ','
               << boolean(controls.espEnemy) << ',' << boolean(controls.espCreature) << ','
               << boolean(controls.espNpc) << ','
               << boolean(controls.espCreatureNormal) << ',' << boolean(controls.espCreatureShiny) << ','
               << boolean(controls.espCreatureBoss) << ',' << boolean(controls.espCreatureElite) << ','
               << boolean(controls.espCreatureVariant) << ',' << boolean(controls.espCreatureMagic) << ','
               << boolean(controls.espCreatureRainbow) << ',' << controls.espSpeciesFilterMode << ','
               << LuaStringLiteral(controls.espSpeciesFilter) << ',' << boolean(controls.espCatchableOnly) << ','
               << boolean(controls.espShowCaptureChance) << ',' << controls.espMinCapturePercent << ','
               << boolean(controls.eggHasteEnabled) << ','
               << boolean(controls.eggHasteEggMovement) << ','
               << controls.eggHasteEggSpeedMultiplier << ',' << boolean(controls.eggHasteDashBoost) << ','
               << controls.eggHasteDashMultiplier << ',' << boolean(controls.eggHasteNoDashCooldown) << ','
               << boolean(controls.eggHasteFastCarryBoost) << ',' << controls.eggHasteFastCarryRatio << ','
               << boolean(controls.eggHasteEspEnabled) << ',' << controls.eggHasteEspRange << ','
               << boolean(controls.eggHasteEspEggs) << ',' << boolean(controls.eggHasteEspNests) << ','
               << boolean(controls.eggHasteEspResources) << ',' << boolean(controls.eggHasteEspLoot) << ','
               << boolean(controls.eggHasteEspBags) << ',' << boolean(controls.eggHasteEspTransfers) << ','
               << boolean(controls.eggHasteEspBreakables) << ','
               << boolean(controls.espShowHealthText) << ',' << boolean(controls.espShowHealthBar) << ','
               << controls.espBoxMode << ',' << boolean(controls.espBoxFill) << ','
               << boolean(controls.espSnaplines) << ',' << boolean(controls.espShinyAlert) << ','
               << boolean(controls.eggHasteRemoveViewFog) << ',' << boolean(controls.eggHasteRevealFullMap) << ','
               << boolean(controls.interconnectionInfiUltimate) << ','
               << boolean(controls.interconnectionCaptureBoost) << ','
               << controls.eggHasteQualityMask << ','
               << boolean(controls.hudUidOverrideEnabled) << ','
               << LuaStringLiteral(controls.hudUidOverrideText) << ')';
        const auto result = ExecuteLua(state, script.str(), "@Arkhan/native-ui-frame");
        if (result.rfind("OK|", 0) == 0) {
            NativeOverlayBridge::Instance().UpdateLuaSnapshot(result.substr(3));
        } else {
            NativeOverlayBridge::Instance().UpdateLuaSnapshot("META\t0\t0\t0\t" + result);
        }
    }
}

std::string Runtime::ExecuteLua(void* state, const std::string& code, const std::string& chunkName) {
    if (executingLua_.test_and_set()) return "ERROR|lua-reentrant";
    struct Guard {
        std::atomic_flag& flag;
        ~Guard() { flag.clear(); }
    } guard{executingLua_};

    if (exportsUnavailable_.load()) {
        if (!nativeLuaSurfaceReady_.load(std::memory_order_acquire) || !luaExecutionReady_.load())
            return "ERROR|native-xlua-surface-not-ready";
        if (code.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return "ERROR|script-too-large";

        const auto readStackString = [&]() -> std::string {
            const char* value = nullptr;
            std::size_t length = 0;
            if (!SafeNativeToString(nativeLuaToLStringFn_, state, -1, &value, &length) || !value)
                return "<non-string-result>";
            constexpr std::size_t maximumResultBytes = 64 * 1024;
            const auto requested = std::min(length, maximumResultBytes);
            if (requested == 0) return {};
            std::string result(requested, '\0');
            std::size_t copied = 0;
            if (!SafeCopyBytes(value, requested, result.data(), result.size(), &copied))
                return "<lua-string-read-failed>";
            result.resize(copied);
            if (length > maximumResultBytes) result += "<truncated>";
            return result;
        };

        int originalTop = -1;
        if (!SafeNativeGetTop(nativeLuaGetTopFn_, state, &originalTop))
            return "ERROR|native-gettop-exception";

        int loadResult = -1;
        if (!SafeNativeLoadBuffer(nativeLuaLoadBufferFn_, state, code.data(),
                                  static_cast<int>(code.size()), chunkName.c_str(), &loadResult)) {
            SafeNativeSetTop(nativeLuaSetTopFn_, state, originalTop);
            return "ERROR|native-load-exception";
        }
        if (loadResult != 0) {
            const auto message = readStackString();
            SafeNativeSetTop(nativeLuaSetTopFn_, state, originalTop);
            return "ERROR|load|" + message;
        }

        int callResult = -1;
        if (!SafeNativePCall(nativeLuaPCallFn_, state, 0, 1, 0, &callResult)) {
            SafeNativeSetTop(nativeLuaSetTopFn_, state, originalTop);
            return "ERROR|native-pcall-exception";
        }
        if (callResult != 0) {
            const auto message = readStackString();
            SafeNativeSetTop(nativeLuaSetTopFn_, state, originalTop);
            return "ERROR|call|" + message;
        }

        const auto result = readStackString();
        if (!SafeNativeSetTop(nativeLuaSetTopFn_, state, originalTop))
            return "ERROR|native-settop-exception";
        return "OK|" + result;
    }

    const auto getTop = luaGetTopFn_;
    const auto setTop = luaSetTopFn_;
    const auto loadBuffer = luaLoadBufferFn_;
    const auto pcall = luaPCallFn_;
    const auto toString = luaToStringFn_;
    if (!getTop || !setTop || !loadBuffer || !pcall || !toString) return "ERROR|lua-pointer-missing";

    const int originalTop = getTop(state, luaGetTopMethod_);
    const auto script = api_.NewString(code);
    const auto name = api_.NewString(chunkName);
    if (!script || !name) return "ERROR|il2cpp-string-new";

    const auto loadResult = loadBuffer(state, script, name, luaLoadBufferMethod_);
    if (loadResult != 0) {
        const auto message = api_.ToUtf8(toString(state, -1, luaToStringMethod_));
        setTop(state, originalTop, luaSetTopMethod_);
        return "ERROR|load|" + message;
    }

    const auto callResult = pcall(state, 0, 1, 0, luaPCallMethod_);
    if (callResult != 0) {
        const auto message = api_.ToUtf8(toString(state, -1, luaToStringMethod_));
        setTop(state, originalTop, luaSetTopMethod_);
        return "ERROR|call|" + message;
    }

    const auto result = api_.ToUtf8(toString(state, -1, luaToStringMethod_));
    setTop(state, originalTop, luaSetTopMethod_);
    return "OK|" + result;
}

std::string Runtime::QueueLua(const std::string& code, const std::string& chunkName, DWORD timeoutMs) {
    if (!dispatcherReady_.load()) return "ERROR|dispatcher-not-ready";
    if (!luaReady_.load()) return "ERROR|lua-state-not-ready";
    if (!luaExecutionReady_.load()) return "ERROR|lua-execution-surface-unavailable";
    auto command = std::make_shared<LuaCommand>();
    command->code = code;
    command->chunkName = chunkName;
    {
        std::lock_guard lock(stateMutex_);
        if (commands_.size() >= 64) return "ERROR|command-queue-full";
        commands_.push_back(command);
    }

    std::unique_lock lock(command->mutex);
    if (!command->completed.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return command->done; }))
        return "ERROR|command-timeout";
    return command->result;
}

std::string Runtime::BootstrapLua() {
    if (bootstrapScript_.empty()) return "ERROR|bootstrap-file-missing";
    bootstrapAttempted_.store(true);
    const auto result = QueueLua(bootstrapScript_, "@Arkhan/Runtime/arkhan_bootstrap.lua", 8000);
    bootstrapReady_.store(result.rfind("OK|ARKHAN_BOOTSTRAP_", 0) == 0);
    Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_MANUAL_RESULT " + result);
    return result;
}

void Runtime::Complete(const std::shared_ptr<LuaCommand>& command, std::string result) {
    {
        std::lock_guard lock(command->mutex);
        command->result = std::move(result);
        command->done = true;
    }
    command->completed.notify_all();
}

bool Runtime::LoadConfiguration() {
    const auto text = ReadTextFile(root_ / L"config.ini");
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char value) { return value == '\r' || value == ' '; }), line.end());
        if (line == "auto_bootstrap=true" || line == "auto_bootstrap=1") autoBootstrap_.store(true);
        if (line == "native_overlay=false" || line == "native_overlay=0" ||
            line == "overlay_enabled=false" || line == "overlay_enabled=0") nativeOverlayEnabled_.store(false);
        if (line == "native_overlay=true" || line == "native_overlay=1" ||
            line == "overlay_enabled=true" || line == "overlay_enabled=1") nativeOverlayEnabled_.store(true);
    }
    Log::Instance().Write("host", std::string("ARKHAN_CONFIG autoBootstrap=") + (autoBootstrap_.load() ? "true" : "false") +
                                  " nativeOverlay=" + (nativeOverlayEnabled_.load() ? "true" : "false"));
    return true;
}

bool Runtime::LoadBootstrapScript() {
    // ARKHAN_NATIVE_AES_GCM_V3_LOAD_BEGIN
    const auto protectedPackage = ReadTextFile(root_ / L"Runtime" / L"arkhan_bootstrap.ark");
    if (!protectedPackage.empty()) {
        bootstrapScript_ = ArkhanDecryptBootstrapV3(protectedPackage);
        if (bootstrapScript_.empty()) {
            Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_DECRYPT_FAILED (invalid package or mismatched DLL)");
            return false;
        }
        Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_PROTECTED_READY");
    } else {
        // Development/reviewer fallback; do not ship plaintext Lua after approval.
        bootstrapScript_ = ReadTextFile(root_ / L"Runtime" / L"arkhan_bootstrap.lua");
    }
    // ARKHAN_NATIVE_AES_GCM_V3_LOAD_END
    if (bootstrapScript_.empty()) {
        Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_FILE_MISSING");
        return false;
    }
    Log::Instance().Write("lua", "ARKHAN_BOOTSTRAP_FILE_READY bytes=" + std::to_string(bootstrapScript_.size()));
    return true;
}


std::filesystem::path Runtime::Root() const {
    return root_;
}



} // namespace arkhan

