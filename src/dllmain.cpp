#include <Windows.h>

#include "arkhan_runtime.h"
#include "version_proxy.h"

namespace {

DWORD WINAPI DeferredStart(void* module) {
    arkhan::Runtime::Instance().Start(static_cast<HMODULE>(module));
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        const auto thread = CreateThread(nullptr, 0, &DeferredStart, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        arkhan::Runtime::Instance().Stop();
    }
    return TRUE;
}
