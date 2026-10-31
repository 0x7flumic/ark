#pragma once

#include "il2cpp_api.h"

#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace arkhan {

class Runtime final {
public:
    static Runtime& Instance();

    bool Start(HMODULE selfModule);
    void Stop();
    std::string QueueLua(const std::string& code, const std::string& chunkName, DWORD timeoutMs = 5000);
    std::string BootstrapLua();
    std::filesystem::path Root() const;

private:
    struct LuaCommand {
        std::string code;
        std::string chunkName;
        std::string result;
        bool done{};
        std::mutex mutex;
        std::condition_variable completed;
    };

    using CommonUpdateFn = void(__fastcall*)(void*, const il2cpp::MethodInfo*);
    using LuaGetTopFn = int(__fastcall*)(void*, const il2cpp::MethodInfo*);
    using LuaSetTopFn = void(__fastcall*)(void*, int, const il2cpp::MethodInfo*);
    using LuaLoadBufferFn = int(__fastcall*)(void*, il2cpp::Il2CppString*, il2cpp::Il2CppString*, const il2cpp::MethodInfo*);
    using LuaPCallFn = int(__fastcall*)(void*, int, int, int, const il2cpp::MethodInfo*);
    using LuaToStringFn = il2cpp::Il2CppString*(__fastcall*)(void*, int, const il2cpp::MethodInfo*);
    using NativeLuaGetTopFn = int(__cdecl*)(void*);
    using NativeLuaSetTopFn = void(__cdecl*)(void*, int);
    using NativeLuaLoadBufferFn = int(__cdecl*)(void*, const char*, int, const char*);
    using NativeLuaPCallFn = int(__cdecl*)(void*, int, int, int);
    using NativeLuaToLStringFn = const char*(__cdecl*)(void*, int, std::size_t*);

    Runtime() = default;
    static DWORD WINAPI BootstrapThreadEntry(void* context);
    static void __fastcall CommonUpdateHook(void* self, const il2cpp::MethodInfo* method);

    void Bootstrap();
    bool ResolveAndInstall();
    bool ResolveKnownRvaAndInstall(HMODULE gameAssembly);
    bool ResolveNativeLuaSurface();
    bool ValidateNativeLuaSurface(void* state);
    void OnCommonUpdate(void* luaManager);
    std::string ExecuteLua(void* state, const std::string& code, const std::string& chunkName);
    void Complete(const std::shared_ptr<LuaCommand>& command, std::string result);
    bool LoadConfiguration();
    bool LoadBootstrapScript();

    HMODULE selfModule_{};
    HANDLE stopEvent_{};
    HANDLE bootstrapThread_{};
    std::filesystem::path root_;
    il2cpp::Api api_;
    void* attachedThread_{};

    il2cpp::Il2CppClass* luaManagerClass_{};
    il2cpp::Il2CppClass* luaEnvClass_{};
    il2cpp::Il2CppClass* luaApiClass_{};
    int32_t luaEnvOffset_{-1};
    int32_t rawLuaOffset_{-1};
    const il2cpp::MethodInfo* commonUpdateMethod_{};
    const il2cpp::MethodInfo* luaGetTopMethod_{};
    const il2cpp::MethodInfo* luaSetTopMethod_{};
    const il2cpp::MethodInfo* luaLoadBufferMethod_{};
    const il2cpp::MethodInfo* luaPCallMethod_{};
    const il2cpp::MethodInfo* luaToStringMethod_{};
    LuaGetTopFn luaGetTopFn_{};
    LuaSetTopFn luaSetTopFn_{};
    LuaLoadBufferFn luaLoadBufferFn_{};
    LuaPCallFn luaPCallFn_{};
    LuaToStringFn luaToStringFn_{};
    NativeLuaGetTopFn nativeLuaGetTopFn_{};
    NativeLuaSetTopFn nativeLuaSetTopFn_{};
    NativeLuaLoadBufferFn nativeLuaLoadBufferFn_{};
    NativeLuaPCallFn nativeLuaPCallFn_{};
    NativeLuaToLStringFn nativeLuaToLStringFn_{};
    il2cpp::VirtualInvokeData* patchedSlot_{};
    CommonUpdateFn originalCommonUpdate_{};

    mutable std::mutex stateMutex_;
    std::deque<std::shared_ptr<LuaCommand>> commands_;
    std::string lastError_;
    std::string discoveryMode_{"pending"};
    std::string luaBridgeMode_{"unresolved"};
    std::string nativeLuaModule_;
    std::string startupPhase_{"created"};
    std::string bootstrapScript_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> proxyReady_{false};
    std::atomic<bool> il2cppReady_{false};
    std::atomic<bool> exportsUnavailable_{false};
    std::atomic<bool> dispatcherReady_{false};
    std::atomic<bool> luaReady_{false};
    std::atomic<bool> luaExecutionReady_{false};
    std::atomic<bool> nativeLuaSurfaceReady_{false};
    std::atomic<bool> nativeLuaProbeAttempted_{false};
    std::atomic<bool> bootstrapReady_{false};
    std::atomic<bool> bootstrapAttempted_{false};
    std::atomic<bool> autoBootstrap_{false};
    std::atomic<bool> nativeOverlayEnabled_{true};
    std::atomic<DWORD> gameThreadId_{0};
    std::atomic<std::uintptr_t> luaState_{0};
    std::atomic<std::uint64_t> hookCalls_{0};
    std::atomic<std::uint64_t> maxHookOverheadUs_{0};
    std::atomic<std::uint64_t> startupGuardRemainingMs_{0};
    std::atomic<std::uint32_t> startupResolveAttempts_{0};
    std::atomic<std::uint64_t> luaStateStableSinceMs_{0};
    std::uint64_t lastNativeUiTickMs_{0};
    std::atomic_flag executingLua_ = ATOMIC_FLAG_INIT;
};

} // namespace arkhan
