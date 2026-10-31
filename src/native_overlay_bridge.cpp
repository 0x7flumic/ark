#include "native_overlay_bridge.h"

#include "arkhan_log.h"

#include <algorithm>
#include <array>
#include <system_error>

namespace arkhan {
namespace {

template <typename T>
T Resolve(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

NativeUiControls ConvertControls(const ArkhanOverlayControlsC& input) {
    NativeUiControls output;
    output.revision = input.revision;
    output.infiniteStamina = input.infiniteStamina != 0;
    output.noFallDamage = input.noFallDamage != 0;
    output.speedBoost = input.speedBoost != 0;
    output.speedMultiplier = input.speedMultiplier;
    output.autoCollect = input.autoCollect != 0;
    output.collectRange = input.collectRange;
    output.autoChest = input.autoChest != 0;
    output.chestRange = input.chestRange;

    output.captureAssistant = input.captureAssistant != 0;
    output.captureAutoThrow = input.captureAutoThrow != 0;
    output.captureFastThrow = input.captureFastThrow != 0;
    output.captureSkipCameraTransition = input.captureSkipCameraTransition != 0;
    output.captureThrowAnimSpeed = input.captureThrowAnimSpeed;
    output.captureBallSpeed = input.captureBallSpeed;
    output.captureRecoveryDelay = input.captureRecoveryDelay;
    output.captureTargetMode = input.captureTargetMode;
    output.captureRange = input.captureRange;
    output.captureMinPercent = input.captureMinPercent;
    output.captureSpecies = input.captureSpecies;

    output.autoDialogueSkip = input.autoDialogueSkip != 0;
    output.autoCutsceneSkip = input.autoCutsceneSkip != 0;
    output.fovOverride = input.fovOverride != 0;
    output.fov = input.fov;
    output.interconnectionGodMode = input.interconnectionGodMode != 0;
    output.interconnectionNoCooldown = input.interconnectionNoCooldown != 0;
    output.interconnectionNoCost = input.interconnectionNoCost != 0;
    output.interconnectionDamage = input.interconnectionDamage != 0;
    output.interconnectionDamageMultiplier = input.interconnectionDamageMultiplier;
    output.interconnectionAttackSpeed = input.interconnectionAttackSpeed != 0;
    output.interconnectionAttackSpeedMultiplier = input.interconnectionAttackSpeedMultiplier;
    output.interconnectionAttackFurious = input.interconnectionAttackFurious != 0;
    output.interconnectionAttackEnhance = input.interconnectionAttackEnhance != 0;
    output.interconnectionAttackHunt = input.interconnectionAttackHunt != 0;

    output.espEnabled = input.espEnabled != 0;
    output.espRange = input.espRange;
    output.espCollect = input.espCollect != 0;
    output.espChest = input.espChest != 0;
    output.espEnemy = input.espEnemy != 0;
    output.espCreature = input.espCreature != 0;
    output.espNpc = input.espNpc != 0;
    output.espCreatureNormal = input.espCreatureNormal != 0;
    output.espCreatureShiny = input.espCreatureShiny != 0;
    output.espCreatureBoss = input.espCreatureBoss != 0;
    output.espCreatureElite = input.espCreatureElite != 0;
    output.espCreatureVariant = input.espCreatureVariant != 0;
    output.espCreatureMagic = input.espCreatureMagic != 0;
    output.espCreatureRainbow = input.espCreatureRainbow != 0;
    output.espSpeciesFilterMode = input.espSpeciesFilterMode;
    output.espSpeciesFilter = input.espSpeciesFilter;
    output.espCatchableOnly = input.espCatchableOnly != 0;
    output.espShowCaptureChance = input.espShowCaptureChance != 0;
    output.espMinCapturePercent = input.espMinCapturePercent;

    output.eggHasteEnabled = input.eggHasteEnabled != 0;
    output.eggHasteEggMovement = input.eggHasteEggMovement != 0;
    output.eggHasteEggSpeedMultiplier = input.eggHasteEggSpeedMultiplier;
    output.eggHasteDashBoost = input.eggHasteDashBoost != 0;
    output.eggHasteDashMultiplier = input.eggHasteDashMultiplier;
    output.eggHasteNoDashCooldown = input.eggHasteNoDashCooldown != 0;
    output.eggHasteFastCarryBoost = input.eggHasteFastCarryBoost != 0;
    output.eggHasteFastCarryRatio = input.eggHasteFastCarryRatio;
    output.eggHasteEspEnabled = input.eggHasteEspEnabled != 0;
    output.eggHasteEspRange = input.eggHasteEspRange;
    output.eggHasteEspEggs = input.eggHasteEspEggs != 0;
    output.eggHasteEspNests = input.eggHasteEspNests != 0;
    output.eggHasteEspResources = input.eggHasteEspResources != 0;
    output.eggHasteEspLoot = input.eggHasteEspLoot != 0;
    output.eggHasteEspBags = input.eggHasteEspBags != 0;
    output.eggHasteEspTransfers = input.eggHasteEspTransfers != 0;
    output.eggHasteEspBreakables = input.eggHasteEspBreakables != 0;
    return output;
}

} // namespace

NativeOverlayBridge& NativeOverlayBridge::Instance() {
    static NativeOverlayBridge bridge;
    return bridge;
}

void ARKHAN_OVERLAY_CALL NativeOverlayBridge::HostLog(const char* category, const char* message) {
    Log::Instance().Write(category ? category : "overlay", message ? message : "");
}

bool NativeOverlayBridge::ResolveExportsLocked(HMODULE module) {
    start_ = Resolve<ArkhanOverlayStartFn>(module, kArkhanOverlayStartExport);
    stop_ = Resolve<ArkhanOverlayStopFn>(module, kArkhanOverlayStopExport);
    ready_ = Resolve<ArkhanOverlayBoolFn>(module, kArkhanOverlayReadyExport);
    menuOpen_ = Resolve<ArkhanOverlayBoolFn>(module, kArkhanOverlayMenuOpenExport);
    getControls_ = Resolve<ArkhanOverlayGetControlsFn>(module, kArkhanOverlayGetControlsExport);
    getEspVisualControls_ = Resolve<ArkhanOverlayGetEspVisualControlsFn>(module, kArkhanOverlayGetEspVisualControlsExport);
    getUidVisualControls_ = Resolve<ArkhanOverlayGetUidVisualControlsFn>(module, kArkhanOverlayGetUidVisualControlsExport);
    getEggHasteAuxControls_ = Resolve<ArkhanOverlayGetEggHasteAuxControlsFn>(module, kArkhanOverlayGetEggHasteAuxControlsExport);
    getEggHasteQualityControls_ = Resolve<ArkhanOverlayGetEggHasteQualityControlsFn>(module, kArkhanOverlayGetEggHasteQualityControlsExport);
    getPlayerGodModeControls_ = Resolve<ArkhanOverlayGetPlayerGodModeControlsFn>(module, kArkhanOverlayGetPlayerGodModeControlsExport);
    getOverloadReliefControls_ = Resolve<ArkhanOverlayGetOverloadReliefControlsFn>(module, kArkhanOverlayGetOverloadReliefControlsExport);
    getInfiUltimateControls_ = Resolve<ArkhanOverlayGetInfiUltimateControlsFn>(module, kArkhanOverlayGetInfiUltimateControlsExport);
    getCaptureBoostControls_ = Resolve<ArkhanOverlayGetCaptureBoostControlsFn>(module, kArkhanOverlayGetCaptureBoostControlsExport);
    getAutofarmControls_ = Resolve<ArkhanOverlayGetAutofarmControlsFn>(module, kArkhanOverlayGetAutofarmControlsExport);
    getAutoquestControls_ = Resolve<ArkhanOverlayGetAutoquestControlsFn>(module, kArkhanOverlayGetAutoquestControlsExport);
    getAutoquestRouteControls_ = Resolve<ArkhanOverlayGetAutoquestRouteControlsFn>(module, kArkhanOverlayGetAutoquestRouteControlsExport);
    getMobVacuumControls_ = Resolve<ArkhanOverlayGetMobVacuumControlsFn>(module, kArkhanOverlayGetMobVacuumControlsExport);
    getMobVacuumMotionControls_ = Resolve<ArkhanOverlayGetMobVacuumMotionControlsFn>(module, kArkhanOverlayGetMobVacuumMotionControlsExport);
    getMobVacuumModeControls_ = Resolve<ArkhanOverlayGetMobVacuumModeControlsFn>(module, kArkhanOverlayGetMobVacuumModeControlsExport);
    updateSnapshot_ = Resolve<ArkhanOverlayUpdateSnapshotFn>(module, kArkhanOverlayUpdateSnapshotExport);
    clearSnapshot_ = Resolve<ArkhanOverlayClearSnapshotFn>(module, kArkhanOverlayClearSnapshotExport);
    getStatus_ = Resolve<ArkhanOverlayGetStatusFn>(module, kArkhanOverlayGetStatusExport);
    return start_ && stop_ && ready_ && menuOpen_ && getControls_ && updateSnapshot_ && clearSnapshot_ && getStatus_;
}

void NativeOverlayBridge::ResetLocked() {
    module_ = nullptr;
    start_ = nullptr;
    stop_ = nullptr;
    ready_ = nullptr;
    menuOpen_ = nullptr;
    getControls_ = nullptr;
    getEspVisualControls_ = nullptr;
    getUidVisualControls_ = nullptr;
    getEggHasteAuxControls_ = nullptr;
    getEggHasteQualityControls_ = nullptr;
    getPlayerGodModeControls_ = nullptr;
    getOverloadReliefControls_ = nullptr;
    getInfiUltimateControls_ = nullptr;
    getCaptureBoostControls_ = nullptr;
    getAutofarmControls_ = nullptr;
    getAutoquestControls_ = nullptr;
    getAutoquestRouteControls_ = nullptr;
    getMobVacuumControls_ = nullptr;
    getMobVacuumMotionControls_ = nullptr;
    getMobVacuumModeControls_ = nullptr;
    updateSnapshot_ = nullptr;
    clearSnapshot_ = nullptr;
    getStatus_ = nullptr;
}

bool NativeOverlayBridge::Start(const std::filesystem::path& arkhanRoot) {
    std::lock_guard lock(mutex_);
    if (module_) return true;

    const auto overlayPath = arkhanRoot / L"ArkhanOverlay.dll";
    if (!std::filesystem::is_regular_file(overlayPath)) {
        status_ = "ArkhanOverlay.dll missing";
        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_MODULE_MISSING path=" + overlayPath.string());
        return false;
    }

    HMODULE module = LoadLibraryW(overlayPath.c_str());
    if (!module) {
        const DWORD error = GetLastError();
        status_ = "LoadLibrary ArkhanOverlay.dll failed: " + std::to_string(error);
        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_LOAD_FAILED win32=" + std::to_string(error));
        return false;
    }

    if (!ResolveExportsLocked(module)) {
        status_ = "ArkhanOverlay.dll export contract mismatch";
        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_EXPORT_MISMATCH");
        ResetLocked();
        FreeLibrary(module);
        return false;
    }

    module_ = module;
    if (!start_(&HostLog, nullptr)) {
        status_ = "ArkhanOverlay_Start failed";
        Log::Instance().Write("overlay", "ARKHAN_OVERLAY_START_EXPORT_FAILED");
        auto stop = stop_;
        HMODULE loaded = module_;
        ResetLocked();
        if (stop) stop();
        FreeLibrary(loaded);
        return false;
    }

    status_ = "ArkhanOverlay.dll loaded; waiting for game swapchain";
    Log::Instance().Write("overlay", "ARKHAN_OVERLAY_MODULE_LOADED boundary=late-load");
    return true;
}

void NativeOverlayBridge::Stop() {
    std::lock_guard lock(mutex_);
    if (!module_) return;
    if (stop_) stop_();
    ResetLocked();
    status_ = "overlay stopped; module parked until process exit";
    Log::Instance().Write("overlay", "ARKHAN_OVERLAY_STOPPED_LATE_MODULE_PARKED");
}

bool NativeOverlayBridge::Loaded() const {
    std::lock_guard lock(mutex_);
    return module_ != nullptr;
}

bool NativeOverlayBridge::Ready() const {
    std::lock_guard lock(mutex_);
    return module_ && ready_ && ready_() != 0;
}

bool NativeOverlayBridge::MenuOpen() const {
    std::lock_guard lock(mutex_);
    return module_ && menuOpen_ && menuOpen_() != 0;
}

NativeUiControls NativeOverlayBridge::ReadControls() const {
    std::lock_guard lock(mutex_);
    ArkhanOverlayControlsC controls{};
    if (!module_ || !getControls_ || !getControls_(&controls)) return NativeUiControls{};
    NativeUiControls output = ConvertControls(controls);
    if (getEspVisualControls_) {
        ArkhanOverlayEspVisualControlsC visual{};
        if (getEspVisualControls_(&visual) && visual.version >= 1u) {
            output.espShowHealthText = visual.showHealthText != 0;
            output.espShowHealthBar = visual.showHealthBar != 0;
            output.espBoxMode = std::clamp(static_cast<int>(visual.boxMode), 0, 2);
            output.espBoxFill = visual.boxFill != 0;
            output.espSnaplines = visual.snaplines != 0;
            output.espShinyAlert = visual.shinyAlert != 0;
        }
    }
    if (getUidVisualControls_) {
        ArkhanOverlayUidVisualControlsC uidVisual{};
        if (getUidVisualControls_(&uidVisual) && uidVisual.version >= 1u) {
            output.hudUidOverrideEnabled = uidVisual.enabled != 0;
            output.hudUidOverrideText = uidVisual.text;
        }
    }
    if (getEggHasteAuxControls_) {
        ArkhanOverlayEggHasteAuxControlsC aux{};
        if (getEggHasteAuxControls_(&aux) && aux.version >= 1u) {
            output.eggHasteRemoveViewFog = aux.removeViewFog != 0;
            output.eggHasteRevealFullMap = aux.revealFullMap != 0;
        }
    }
    if (getEggHasteQualityControls_) {
        ArkhanOverlayEggHasteQualityControlsC quality{};
        if (getEggHasteQualityControls_(&quality) && quality.version >= 1u)
            output.eggHasteQualityMask = quality.qualityMask & 0x7fu;
    }
    if (getPlayerGodModeControls_) {
        ArkhanOverlayPlayerGodModeControlsC playerGodMode{};
        if (getPlayerGodModeControls_(&playerGodMode) && playerGodMode.version >= 1u)
            output.playerGodMode = playerGodMode.enabled != 0;
    }
    if (getOverloadReliefControls_) {
        ArkhanOverlayOverloadReliefControlsC overloadRelief{};
        if (getOverloadReliefControls_(&overloadRelief) && overloadRelief.version >= 1u)
            output.playerOverloadRelief = overloadRelief.enabled != 0;
    }
    if (getInfiUltimateControls_) {
        ArkhanOverlayInfiUltimateControlsC ultimate{};
        if (getInfiUltimateControls_(&ultimate) && ultimate.version >= 1u)
            output.interconnectionInfiUltimate = ultimate.enabled != 0;
    }
    if (getCaptureBoostControls_) {
        ArkhanOverlayCaptureBoostControlsC capture{};
        if (getCaptureBoostControls_(&capture) && capture.version >= 1u) {
            output.interconnectionCaptureBoost = capture.enabled != 0;
        }
    }
    if (getAutofarmControls_) {
        ArkhanOverlayAutofarmControlsC farm{};
        if (getAutofarmControls_(&farm) && farm.version >= 1u) {
            output.autofarmEnabled = farm.enabled != 0;
            output.autofarmIncludeChests = farm.includeChests != 0;
            output.autofarmIncludeAmber = farm.includeAmber != 0;
            output.autofarmScanRadius = std::clamp(static_cast<int>(farm.scanRadius), 50, 800);
            output.autofarmEmptyConfirmRadius = std::clamp(static_cast<int>(farm.emptyConfirmRadius), 25, output.autofarmScanRadius);
            output.autofarmTeleportDelayMs = std::clamp<std::uint32_t>(farm.teleportDelayMs, 250u, 5000u);
            output.autofarmSettleDelayMs = std::clamp<std::uint32_t>(farm.settleDelayMs, 500u, 8000u);
            output.autofarmResetRequest = farm.resetRequest;
        }
    }
    if (getAutoquestControls_) {
        ArkhanOverlayAutoquestControlsC quest{};
        if (getAutoquestControls_(&quest) && quest.version >= 1u) {
            output.autoquestEnabled = quest.enabled != 0;
            output.autoquestSelectedQuestId = quest.selectedQuestId;
            output.autoquestTeleportRequest = quest.teleportRequest;
        }
    }
    if (getAutoquestRouteControls_) {
        ArkhanOverlayAutoquestRouteControlsC route{};
        if (getAutoquestRouteControls_(&route) && route.version >= 1u) {
            output.autoquestSelectedTemplateId = route.selectedTemplateId;
            output.autoquestSelectedMarkId = route.selectedMarkId;
            output.autoquestSelectedMarkRealSceneId = route.selectedMarkRealSceneId;
        }
    }
    if (getMobVacuumControls_) {
        ArkhanOverlayMobVacuumControlsC vacuum{};
        if (getMobVacuumControls_(&vacuum) && vacuum.version >= 1u) {
            output.mobVacuumEnabled = vacuum.enabled != 0;
            output.mobVacuumRadius = std::clamp(static_cast<int>(vacuum.radius), 1, 500);
            output.mobVacuumMaxMobs = std::clamp(static_cast<int>(vacuum.maxMobs), 1, 32);
            output.mobVacuumDistance = std::clamp(vacuum.distance, 0.5f, 10.0f);
        }
    }
    if (getMobVacuumMotionControls_) {
        ArkhanOverlayMobVacuumMotionControlsC motion{};
        if (getMobVacuumMotionControls_(&motion) && motion.version >= 1u) {
            output.mobVacuumRepositionTolerance = std::clamp(motion.repositionTolerance, 0.25f, 10.0f);
        }
    }
    if (getMobVacuumModeControls_) {
        ArkhanOverlayMobVacuumModeControlsC mode{};
        if (getMobVacuumModeControls_(&mode) && mode.version >= 1u) {
            output.mobVacuumStun = mode.stun != 0;
        }
    }
    return output;
}

void NativeOverlayBridge::UpdateLuaSnapshot(const std::string& protocol) {
    std::lock_guard lock(mutex_);
    if (module_ && updateSnapshot_) updateSnapshot_(protocol.c_str());
}

void NativeOverlayBridge::ClearLuaSnapshot() {
    std::lock_guard lock(mutex_);
    if (module_ && clearSnapshot_) clearSnapshot_();
}

std::string NativeOverlayBridge::StatusText() const {
    std::lock_guard lock(mutex_);
    if (!module_ || !getStatus_) return status_;
    std::array<char, 512> buffer{};
    getStatus_(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
    return buffer.data();
}

} // namespace arkhan
