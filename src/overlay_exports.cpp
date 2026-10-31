#include <Windows.h>

#include "overlay_api.h"
#include "overlay_runtime.h"
#include "native_overlay.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>

namespace {
ArkhanOverlayControlsC ToExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayControlsC output{};
    output.revision = input.revision;
    output.infiniteStamina = input.infiniteStamina ? 1u : 0u;
    output.noFallDamage = input.noFallDamage ? 1u : 0u;
    output.speedBoost = input.speedBoost ? 1u : 0u;
    output.speedMultiplier = input.speedMultiplier;
    output.autoCollect = input.autoCollect ? 1u : 0u;
    output.collectRange = input.collectRange;
    output.autoChest = input.autoChest ? 1u : 0u;
    output.chestRange = input.chestRange;

    output.captureAssistant = input.captureAssistant ? 1u : 0u;
    output.captureAutoThrow = input.captureAutoThrow ? 1u : 0u;
    output.captureFastThrow = input.captureFastThrow ? 1u : 0u;
    output.captureSkipCameraTransition = input.captureSkipCameraTransition ? 1u : 0u;
    output.captureThrowAnimSpeed = input.captureThrowAnimSpeed;
    output.captureBallSpeed = input.captureBallSpeed;
    output.captureRecoveryDelay = input.captureRecoveryDelay;
    output.captureTargetMode = input.captureTargetMode;
    output.captureRange = input.captureRange;
    output.captureMinPercent = input.captureMinPercent;
    std::snprintf(output.captureSpecies, sizeof(output.captureSpecies), "%s", input.captureSpecies.c_str());

    output.autoDialogueSkip = input.autoDialogueSkip ? 1u : 0u;
    output.autoCutsceneSkip = input.autoCutsceneSkip ? 1u : 0u;
    output.fovOverride = input.fovOverride ? 1u : 0u;
    output.fov = input.fov;
    output.interconnectionGodMode = input.interconnectionGodMode ? 1u : 0u;
    output.interconnectionNoCooldown = input.interconnectionNoCooldown ? 1u : 0u;
    output.interconnectionNoCost = input.interconnectionNoCost ? 1u : 0u;
    output.interconnectionDamage = input.interconnectionDamage ? 1u : 0u;
    output.interconnectionDamageMultiplier = input.interconnectionDamageMultiplier;
    output.interconnectionAttackSpeed = input.interconnectionAttackSpeed ? 1u : 0u;
    output.interconnectionAttackSpeedMultiplier = input.interconnectionAttackSpeedMultiplier;
    output.interconnectionAttackFurious = input.interconnectionAttackFurious ? 1u : 0u;
    output.interconnectionAttackEnhance = input.interconnectionAttackEnhance ? 1u : 0u;
    output.interconnectionAttackHunt = input.interconnectionAttackHunt ? 1u : 0u;

    output.espEnabled = input.espEnabled ? 1u : 0u;
    output.espRange = input.espRange;
    output.espCollect = input.espCollect ? 1u : 0u;
    output.espChest = input.espChest ? 1u : 0u;
    output.espEnemy = input.espEnemy ? 1u : 0u;
    output.espCreature = input.espCreature ? 1u : 0u;
    output.espNpc = input.espNpc ? 1u : 0u;
    output.espCreatureNormal = input.espCreatureNormal ? 1u : 0u;
    output.espCreatureShiny = input.espCreatureShiny ? 1u : 0u;
    output.espCreatureBoss = input.espCreatureBoss ? 1u : 0u;
    output.espCreatureElite = input.espCreatureElite ? 1u : 0u;
    output.espCreatureVariant = input.espCreatureVariant ? 1u : 0u;
    output.espCreatureMagic = input.espCreatureMagic ? 1u : 0u;
    output.espCreatureRainbow = input.espCreatureRainbow ? 1u : 0u;
    output.espSpeciesFilterMode = input.espSpeciesFilterMode;
    std::snprintf(output.espSpeciesFilter, sizeof(output.espSpeciesFilter), "%s", input.espSpeciesFilter.c_str());
    output.espCatchableOnly = input.espCatchableOnly ? 1u : 0u;
    output.espShowCaptureChance = input.espShowCaptureChance ? 1u : 0u;
    output.espMinCapturePercent = input.espMinCapturePercent;

    output.eggHasteEnabled = input.eggHasteEnabled ? 1u : 0u;
    output.eggHasteEggMovement = input.eggHasteEggMovement ? 1u : 0u;
    output.eggHasteEggSpeedMultiplier = input.eggHasteEggSpeedMultiplier;
    output.eggHasteDashBoost = input.eggHasteDashBoost ? 1u : 0u;
    output.eggHasteDashMultiplier = input.eggHasteDashMultiplier;
    output.eggHasteNoDashCooldown = input.eggHasteNoDashCooldown ? 1u : 0u;
    output.eggHasteFastCarryBoost = input.eggHasteFastCarryBoost ? 1u : 0u;
    output.eggHasteFastCarryRatio = input.eggHasteFastCarryRatio;
    output.eggHasteEspEnabled = input.eggHasteEspEnabled ? 1u : 0u;
    output.eggHasteEspRange = input.eggHasteEspRange;
    output.eggHasteEspEggs = input.eggHasteEspEggs ? 1u : 0u;
    output.eggHasteEspNests = input.eggHasteEspNests ? 1u : 0u;
    output.eggHasteEspResources = input.eggHasteEspResources ? 1u : 0u;
    output.eggHasteEspLoot = input.eggHasteEspLoot ? 1u : 0u;
    output.eggHasteEspBags = input.eggHasteEspBags ? 1u : 0u;
    output.eggHasteEspTransfers = input.eggHasteEspTransfers ? 1u : 0u;
    output.eggHasteEspBreakables = input.eggHasteEspBreakables ? 1u : 0u;
    return output;
}

ArkhanOverlayEspVisualControlsC ToEspVisualExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayEspVisualControlsC output{};
    output.version = 1u;
    output.showHealthText = input.espShowHealthText ? 1u : 0u;
    output.showHealthBar = input.espShowHealthBar ? 1u : 0u;
    output.boxMode = std::clamp(input.espBoxMode, 0, 2);
    output.boxFill = input.espBoxFill ? 1u : 0u;
    output.snaplines = input.espSnaplines ? 1u : 0u;
    output.shinyAlert = input.espShinyAlert ? 1u : 0u;
    return output;
}

ArkhanOverlayUidVisualControlsC ToUidVisualExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayUidVisualControlsC output{};
    output.version = 1u;
    output.enabled = input.hudUidOverrideEnabled ? 1u : 0u;
    std::snprintf(output.text, sizeof(output.text), "%s", input.hudUidOverrideText.c_str());
    return output;
}

ArkhanOverlayEggHasteAuxControlsC ToEggHasteAuxExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayEggHasteAuxControlsC output{};
    output.version = 1u;
    output.removeViewFog = input.eggHasteRemoveViewFog ? 1u : 0u;
    output.revealFullMap = input.eggHasteRevealFullMap ? 1u : 0u;
    return output;
}

ArkhanOverlayEggHasteQualityControlsC ToEggHasteQualityExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayEggHasteQualityControlsC output{};
    output.version = 1u;
    output.qualityMask = 0u;
    if (input.eggHasteQualityWhite) output.qualityMask |= 1u << 0u;
    if (input.eggHasteQualityGreen) output.qualityMask |= 1u << 1u;
    if (input.eggHasteQualityBlue) output.qualityMask |= 1u << 2u;
    if (input.eggHasteQualityPurple) output.qualityMask |= 1u << 3u;
    if (input.eggHasteQualityGold) output.qualityMask |= 1u << 4u;
    if (input.eggHasteQualityRainbow) output.qualityMask |= 1u << 5u;
    if (input.eggHasteQualityUnknown) output.qualityMask |= 1u << 6u;
    return output;
}






ArkhanOverlayPlayerGodModeControlsC ToPlayerGodModeExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayPlayerGodModeControlsC output{};
    output.version = 1u;
    output.enabled = input.playerGodMode ? 1u : 0u;
    return output;
}

ArkhanOverlayOverloadReliefControlsC ToOverloadReliefExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayOverloadReliefControlsC output{};
    output.version = 1u;
    output.enabled = input.playerOverloadRelief ? 1u : 0u;
    return output;
}

ArkhanOverlayInfiUltimateControlsC ToInfiUltimateExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayInfiUltimateControlsC output{};
    output.version = 1u;
    output.enabled = input.interconnectionInfiUltimate ? 1u : 0u;
    return output;
}

ArkhanOverlayCaptureBoostControlsC ToCaptureBoostExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayCaptureBoostControlsC output{};
    output.version = 1u;
    output.enabled = input.interconnectionCaptureBoost ? 1u : 0u;
    return output;
}

ArkhanOverlayAutofarmControlsC ToAutofarmExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayAutofarmControlsC output{};
    output.version = 1u;
    output.enabled = input.autofarmEnabled ? 1u : 0u;
    output.includeChests = input.autofarmIncludeChests ? 1u : 0u;
    output.includeAmber = input.autofarmIncludeAmber ? 1u : 0u;
    output.scanRadius = input.autofarmScanRadius;
    output.emptyConfirmRadius = input.autofarmEmptyConfirmRadius;
    output.teleportDelayMs = input.autofarmTeleportDelayMs;
    output.settleDelayMs = input.autofarmSettleDelayMs;
    output.resetRequest = input.autofarmResetRequest;
    return output;
}

ArkhanOverlayAutoquestControlsC ToAutoquestExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayAutoquestControlsC output{};
    output.version = 1u;
    output.enabled = input.autoquestEnabled ? 1u : 0u;
    output.selectedQuestId = input.autoquestSelectedQuestId;
    output.teleportRequest = input.autoquestTeleportRequest;
    return output;
}

ArkhanOverlayAutoquestRouteControlsC ToAutoquestRouteExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayAutoquestRouteControlsC output{};
    output.version = 1u;
    output.selectedTemplateId = input.autoquestSelectedTemplateId;
    output.selectedMarkId = input.autoquestSelectedMarkId;
    output.selectedMarkRealSceneId = input.autoquestSelectedMarkRealSceneId;
    return output;
}

ArkhanOverlayMobVacuumControlsC ToMobVacuumExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayMobVacuumControlsC output{};
    output.version = 1u;
    output.enabled = input.mobVacuumEnabled ? 1u : 0u;
    output.radius = input.mobVacuumRadius;
    output.maxMobs = input.mobVacuumMaxMobs;
    output.distance = input.mobVacuumDistance;
    return output;
}

ArkhanOverlayMobVacuumMotionControlsC ToMobVacuumMotionExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayMobVacuumMotionControlsC output{};
    output.version = 1u;
    output.repositionTolerance = input.mobVacuumRepositionTolerance;
    return output;
}

ArkhanOverlayMobVacuumModeControlsC ToMobVacuumModeExternal(const arkhan::NativeUiControls& input) {
    ArkhanOverlayMobVacuumModeControlsC output{};
    output.version = 1u;
    output.stun = input.mobVacuumStun ? 1u : 0u;
    return output;
}

}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_Start(ArkhanOverlayLogFn logCallback, ArkhanOverlayExitFn exitCallback) {
    arkhan::SetOverlayLogCallback(logCallback);
    arkhan::SetOverlayExitCallback(exitCallback);
    return arkhan::NativeOverlay::Instance().Start() ? 1 : 0;
}

ARKHAN_OVERLAY_EXPORT void ARKHAN_OVERLAY_CALL ArkhanOverlay_Stop() {
    arkhan::NativeOverlay::Instance().Stop();
    arkhan::SetOverlayLogCallback(nullptr);
    arkhan::SetOverlayExitCallback(nullptr);
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_Ready() {
    return arkhan::NativeOverlay::Instance().Ready() ? 1 : 0;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_MenuOpen() {
    return arkhan::NativeOverlay::Instance().MenuOpen() ? 1 : 0;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetControls(ArkhanOverlayControlsC* controls) {
    if (!controls) return 0;
    *controls = ToExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetEspVisualControls(ArkhanOverlayEspVisualControlsC* controls) {
    if (!controls) return 0;
    *controls = ToEspVisualExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetUidVisualControls(ArkhanOverlayUidVisualControlsC* controls) {
    if (!controls) return 0;
    *controls = ToUidVisualExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetEggHasteAuxControls(ArkhanOverlayEggHasteAuxControlsC* controls) {
    if (!controls) return 0;
    *controls = ToEggHasteAuxExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetEggHasteQualityControls(ArkhanOverlayEggHasteQualityControlsC* controls) {
    if (!controls) return 0;
    *controls = ToEggHasteQualityExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}








ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetPlayerGodModeControls(ArkhanOverlayPlayerGodModeControlsC* controls) {
    if (!controls) return 0;
    *controls = ToPlayerGodModeExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetOverloadReliefControls(ArkhanOverlayOverloadReliefControlsC* controls) {
    if (!controls) return 0;
    *controls = ToOverloadReliefExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}


ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetInfiUltimateControls(ArkhanOverlayInfiUltimateControlsC* controls) {
    if (!controls) return 0;
    *controls = ToInfiUltimateExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetCaptureBoostControls(ArkhanOverlayCaptureBoostControlsC* controls) {
    if (!controls) return 0;
    *controls = ToCaptureBoostExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetAutofarmControls(ArkhanOverlayAutofarmControlsC* controls) {
    if (!controls) return 0;
    *controls = ToAutofarmExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetAutoquestControls(ArkhanOverlayAutoquestControlsC* controls) {
    if (!controls) return 0;
    *controls = ToAutoquestExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetAutoquestRouteControls(ArkhanOverlayAutoquestRouteControlsC* controls) {
    if (!controls) return 0;
    *controls = ToAutoquestRouteExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetMobVacuumControls(ArkhanOverlayMobVacuumControlsC* controls) {
    if (!controls) return 0;
    *controls = ToMobVacuumExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetMobVacuumMotionControls(ArkhanOverlayMobVacuumMotionControlsC* controls) {
    if (!controls) return 0;
    *controls = ToMobVacuumMotionExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT int ARKHAN_OVERLAY_CALL ArkhanOverlay_GetMobVacuumModeControls(ArkhanOverlayMobVacuumModeControlsC* controls) {
    if (!controls) return 0;
    *controls = ToMobVacuumModeExternal(arkhan::NativeOverlay::Instance().ReadControls());
    return 1;
}

ARKHAN_OVERLAY_EXPORT void ARKHAN_OVERLAY_CALL ArkhanOverlay_UpdateSnapshot(const char* protocol) {
    arkhan::NativeOverlay::Instance().UpdateLuaSnapshot(protocol ? protocol : "");
}

ARKHAN_OVERLAY_EXPORT void ARKHAN_OVERLAY_CALL ArkhanOverlay_ClearSnapshot() {
    arkhan::NativeOverlay::Instance().ClearLuaSnapshot();
}

ARKHAN_OVERLAY_EXPORT void ARKHAN_OVERLAY_CALL ArkhanOverlay_GetStatus(char* buffer, std::uint32_t capacity) {
    if (!buffer || capacity == 0) return;
    const std::string status = arkhan::NativeOverlay::Instance().StatusText();
    const auto count = std::min<std::size_t>(status.size(), capacity - 1u);
    if (count) std::memcpy(buffer, status.data(), count);
    buffer[count] = '\0';
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
