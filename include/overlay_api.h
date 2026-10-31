#pragma once

#include <cstdint>

#if defined(_WIN32)
#define ARKHAN_OVERLAY_CALL __stdcall
#define ARKHAN_OVERLAY_EXPORT extern "C" __declspec(dllexport)
#else
#define ARKHAN_OVERLAY_CALL
#define ARKHAN_OVERLAY_EXPORT extern "C"
#endif

struct ArkhanOverlayControlsC {
    std::uint64_t revision{1};
    std::uint32_t infiniteStamina{};
    std::uint32_t noFallDamage{};
    std::uint32_t speedBoost{};
    float speedMultiplier{1.50f};
    std::uint32_t autoCollect{};
    std::int32_t collectRange{30};
    std::uint32_t autoChest{};
    std::int32_t chestRange{18};

    std::uint32_t captureAssistant{};
    std::uint32_t captureAutoThrow{};
    std::uint32_t captureFastThrow{1};
    std::uint32_t captureSkipCameraTransition{1};
    float captureThrowAnimSpeed{2.00f};
    float captureBallSpeed{2.00f};
    float captureRecoveryDelay{0.30f};
    std::int32_t captureTargetMode{0}; // 0 nearest, 1 ESP filtered, 2 species, 3 game-marked/locked
    std::int32_t captureRange{50};
    std::int32_t captureMinPercent{0};
    char captureSpecies[96]{};

    std::uint32_t autoDialogueSkip{};
    std::uint32_t autoCutsceneSkip{};
    std::uint32_t fovOverride{};
    std::int32_t fov{80};
    std::uint32_t interconnectionGodMode{};
    std::uint32_t interconnectionNoCooldown{};
    std::uint32_t interconnectionNoCost{};
    std::uint32_t interconnectionDamage{};
    float interconnectionDamageMultiplier{2.00f};
    std::uint32_t interconnectionAttackSpeed{};
    float interconnectionAttackSpeedMultiplier{1.50f};
    std::uint32_t interconnectionAttackFurious{};
    std::uint32_t interconnectionAttackEnhance{};
    std::uint32_t interconnectionAttackHunt{};

    std::uint32_t espEnabled{};
    std::int32_t espRange{150};
    std::uint32_t espCollect{1};
    std::uint32_t espChest{1};
    std::uint32_t espEnemy{1};
    std::uint32_t espCreature{1};
    std::uint32_t espNpc{1};
    std::uint32_t espCreatureNormal{1};
    std::uint32_t espCreatureShiny{1};
    std::uint32_t espCreatureBoss{1};
    std::uint32_t espCreatureElite{1};
    std::uint32_t espCreatureVariant{1};
    std::uint32_t espCreatureMagic{1};
    std::uint32_t espCreatureRainbow{1};
    std::int32_t espSpeciesFilterMode{0}; // 0 all, 1 whitelist, 2 blacklist
    char espSpeciesFilter[160]{};
    std::uint32_t espCatchableOnly{};
    std::uint32_t espShowCaptureChance{};
    std::int32_t espMinCapturePercent{0};

    std::uint32_t eggHasteEnabled{};
    std::uint32_t eggHasteEggMovement{};
    float eggHasteEggSpeedMultiplier{1.50f};
    std::uint32_t eggHasteDashBoost{};
    float eggHasteDashMultiplier{1.50f};
    std::uint32_t eggHasteNoDashCooldown{};
    std::uint32_t eggHasteFastCarryBoost{};
    float eggHasteFastCarryRatio{1.40f};
    std::uint32_t eggHasteEspEnabled{};
    std::int32_t eggHasteEspRange{200};
    std::uint32_t eggHasteEspEggs{1};
    std::uint32_t eggHasteEspNests{1};
    std::uint32_t eggHasteEspResources{1};
    std::uint32_t eggHasteEspLoot{1};
    std::uint32_t eggHasteEspBags{1};
    std::uint32_t eggHasteEspTransfers{1};
    std::uint32_t eggHasteEspBreakables{};
};

struct ArkhanOverlayEspVisualControlsC {
    std::uint32_t version{1};
    std::uint32_t showHealthText{};
    std::uint32_t showHealthBar{};
    std::int32_t boxMode{}; // 0 off, 1 full, 2 corners
    std::uint32_t boxFill{};
    std::uint32_t snaplines{};
    std::uint32_t shinyAlert{};
};

struct ArkhanOverlayUidVisualControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
    char text[96]{};
};

struct ArkhanOverlayEggHasteAuxControlsC {
    std::uint32_t version{1};
    std::uint32_t removeViewFog{};
    std::uint32_t revealFullMap{};
};

struct ArkhanOverlayEggHasteQualityControlsC {
    std::uint32_t version{1};
    std::uint32_t qualityMask{0x7fu};
};

struct ArkhanOverlayPlayerGodModeControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
};

struct ArkhanOverlayOverloadReliefControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
};

struct ArkhanOverlayInfiUltimateControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
};

struct ArkhanOverlayCaptureBoostControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
};

struct ArkhanOverlayAutofarmControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
    std::uint32_t includeChests{1};
    std::uint32_t includeAmber{1};
    std::int32_t scanRadius{500};
    std::int32_t emptyConfirmRadius{180};
    std::uint32_t teleportDelayMs{900};
    std::uint32_t settleDelayMs{1500};
    std::uint32_t resetRequest{};
};

struct ArkhanOverlayAutoquestControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
    std::int32_t selectedQuestId{};
    std::uint32_t teleportRequest{};
};

struct ArkhanOverlayAutoquestRouteControlsC {
    std::uint32_t version{1};
    std::int32_t selectedTemplateId{};
    std::int32_t selectedMarkId{};
    std::int32_t selectedMarkRealSceneId{};
};

struct ArkhanOverlayMobVacuumControlsC {
    std::uint32_t version{1};
    std::uint32_t enabled{};
    std::int32_t radius{100};
    std::int32_t maxMobs{12};
    float distance{4.0f};
};

struct ArkhanOverlayMobVacuumMotionControlsC {
    std::uint32_t version{1};
    float repositionTolerance{1.5f};
};

struct ArkhanOverlayMobVacuumModeControlsC {
    std::uint32_t version{1};
    std::uint32_t stun{};
};

using ArkhanOverlayLogFn = void(ARKHAN_OVERLAY_CALL*)(const char* category, const char* message);
using ArkhanOverlayExitFn = void(ARKHAN_OVERLAY_CALL*)();

using ArkhanOverlayStartFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayLogFn logCallback, ArkhanOverlayExitFn exitCallback);
using ArkhanOverlayStopFn = void(ARKHAN_OVERLAY_CALL*)();
using ArkhanOverlayBoolFn = int(ARKHAN_OVERLAY_CALL*)();
using ArkhanOverlayGetControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayControlsC* controls);
using ArkhanOverlayGetEspVisualControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayEspVisualControlsC* controls);
using ArkhanOverlayGetUidVisualControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayUidVisualControlsC* controls);
using ArkhanOverlayGetEggHasteAuxControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayEggHasteAuxControlsC* controls);
using ArkhanOverlayGetEggHasteQualityControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayEggHasteQualityControlsC* controls);
using ArkhanOverlayGetPlayerGodModeControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayPlayerGodModeControlsC* controls);
using ArkhanOverlayGetOverloadReliefControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayOverloadReliefControlsC* controls);
using ArkhanOverlayGetInfiUltimateControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayInfiUltimateControlsC* controls);
using ArkhanOverlayGetCaptureBoostControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayCaptureBoostControlsC* controls);
using ArkhanOverlayGetAutofarmControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayAutofarmControlsC* controls);
using ArkhanOverlayGetAutoquestControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayAutoquestControlsC* controls);
using ArkhanOverlayGetAutoquestRouteControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayAutoquestRouteControlsC* controls);
using ArkhanOverlayGetMobVacuumControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayMobVacuumControlsC* controls);
using ArkhanOverlayGetMobVacuumMotionControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayMobVacuumMotionControlsC* controls);
using ArkhanOverlayGetMobVacuumModeControlsFn = int(ARKHAN_OVERLAY_CALL*)(ArkhanOverlayMobVacuumModeControlsC* controls);
using ArkhanOverlayUpdateSnapshotFn = void(ARKHAN_OVERLAY_CALL*)(const char* protocol);
using ArkhanOverlayClearSnapshotFn = void(ARKHAN_OVERLAY_CALL*)();
using ArkhanOverlayGetStatusFn = void(ARKHAN_OVERLAY_CALL*)(char* buffer, std::uint32_t capacity);

inline constexpr const char* kArkhanOverlayStartExport = "ArkhanOverlay_Start";
inline constexpr const char* kArkhanOverlayStopExport = "ArkhanOverlay_Stop";
inline constexpr const char* kArkhanOverlayReadyExport = "ArkhanOverlay_Ready";
inline constexpr const char* kArkhanOverlayMenuOpenExport = "ArkhanOverlay_MenuOpen";
inline constexpr const char* kArkhanOverlayGetControlsExport = "ArkhanOverlay_GetControls";
inline constexpr const char* kArkhanOverlayGetEspVisualControlsExport = "ArkhanOverlay_GetEspVisualControls";
inline constexpr const char* kArkhanOverlayGetUidVisualControlsExport = "ArkhanOverlay_GetUidVisualControls";
inline constexpr const char* kArkhanOverlayGetEggHasteAuxControlsExport = "ArkhanOverlay_GetEggHasteAuxControls";
inline constexpr const char* kArkhanOverlayGetEggHasteQualityControlsExport = "ArkhanOverlay_GetEggHasteQualityControls";
inline constexpr const char* kArkhanOverlayGetPlayerGodModeControlsExport = "ArkhanOverlay_GetPlayerGodModeControls";
inline constexpr const char* kArkhanOverlayGetOverloadReliefControlsExport = "ArkhanOverlay_GetOverloadReliefControls";
inline constexpr const char* kArkhanOverlayGetInfiUltimateControlsExport = "ArkhanOverlay_GetInfiUltimateControls";
inline constexpr const char* kArkhanOverlayGetCaptureBoostControlsExport = "ArkhanOverlay_GetCaptureBoostControls";
inline constexpr const char* kArkhanOverlayGetAutofarmControlsExport = "ArkhanOverlay_GetAutofarmControls";
inline constexpr const char* kArkhanOverlayGetAutoquestControlsExport = "ArkhanOverlay_GetAutoquestControls";
inline constexpr const char* kArkhanOverlayGetAutoquestRouteControlsExport = "ArkhanOverlay_GetAutoquestRouteControls";
inline constexpr const char* kArkhanOverlayGetMobVacuumControlsExport = "ArkhanOverlay_GetMobVacuumControls";
inline constexpr const char* kArkhanOverlayGetMobVacuumMotionControlsExport = "ArkhanOverlay_GetMobVacuumMotionControls";
inline constexpr const char* kArkhanOverlayGetMobVacuumModeControlsExport = "ArkhanOverlay_GetMobVacuumModeControls";
inline constexpr const char* kArkhanOverlayUpdateSnapshotExport = "ArkhanOverlay_UpdateSnapshot";
inline constexpr const char* kArkhanOverlayClearSnapshotExport = "ArkhanOverlay_ClearSnapshot";
inline constexpr const char* kArkhanOverlayGetStatusExport = "ArkhanOverlay_GetStatus";
