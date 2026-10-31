#pragma once

#include "overlay_api.h"

#include <string>

namespace arkhan {

void SetOverlayLogCallback(ArkhanOverlayLogFn callback);
void SetOverlayExitCallback(ArkhanOverlayExitFn callback);
void OverlayExitRequested();
void OverlayLog(const char* category, const std::string& message);

} // namespace arkhan
