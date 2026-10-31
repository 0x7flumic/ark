#include "overlay_runtime.h"

#include <Windows.h>
#include <atomic>
#include <string>

namespace arkhan {
namespace {
std::atomic<ArkhanOverlayLogFn> g_logCallback{nullptr};
std::atomic<ArkhanOverlayExitFn> g_exitCallback{nullptr};
}

void SetOverlayExitCallback(ArkhanOverlayExitFn callback) {
    g_exitCallback.store(callback, std::memory_order_release);
}

void OverlayExitRequested() {
    if (auto callback = g_exitCallback.load(std::memory_order_acquire)) callback();
}

void SetOverlayLogCallback(ArkhanOverlayLogFn callback) {
    g_logCallback.store(callback, std::memory_order_release);
}

void OverlayLog(const char* category, const std::string& message) {
    if (auto callback = g_logCallback.load(std::memory_order_acquire)) {
        callback(category ? category : "overlay", message.c_str());
        return;
    }
    std::string line = "[ArkhanOverlay] [";
    line += category ? category : "overlay";
    line += "] ";
    line += message;
    line += "\n";
    OutputDebugStringA(line.c_str());
}

} // namespace arkhan
