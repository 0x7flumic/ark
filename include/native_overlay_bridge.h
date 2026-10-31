#pragma once

#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

#include "overlay_api.h"

namespace arkhan {

struct NativeUiControls {
    std::uint64_t revision{1};
    bool infiniteStamina{false};
    bool noFallDamage{false};
    bool speedBoost{false};
    float speedMultiplier{1.50f};
    bool autoCollect{false};
    int collectRange{30};
    bool autoChest{false};
    int chestRange{18};
    bool playerGodMode{false};
    bool playerOverloadRelief{false};

    bool captureAssistant{false};
    bool captureAutoThrow{false};
    bool captureFastThrow{false};
    bool captureSkipCameraTransition{false};
    float captureThrowAnimSpeed{2.00f};
    float captureBallSpeed{2.00f};
    float captureRecoveryDelay{0.30f};
    int captureTargetMode{0};
    int captureRange{50};
    int captureMinPercent{0};
    std::string captureSpecies;

    bool autoDialogueSkip{false};
    bool autoCutsceneSkip{false};
    bool fovOverride{false};
    int fov{80};
    bool interconnectionGodMode{false};
    bool interconnectionNoCooldown{false};
    bool interconnectionNoCost{false};
    bool interconnectionDamage{false};
    float interconnectionDamageMultiplier{2.00f};
    bool interconnectionAttackSpeed{false};
    float interconnectionAttackSpeedMultiplier{1.50f};
    bool interconnectionAttackFurious{false};
    bool interconnectionAttackEnhance{false};
    bool interconnectionAttackHunt{false};
    bool interconnectionCaptureBoost{false};

    bool interconnectionInfiUltimate{false};

    bool espEnabled{false};
    int espRange{150};
    bool espCollect{false};
    bool espChest{false};
    bool espEnemy{false};
    bool espCreature{false};
    bool espNpc{false};
    bool espCreatureNormal{false};
    bool espCreatureShiny{false};
    bool espCreatureBoss{false};
    bool espCreatureElite{false};
    bool espCreatureVariant{false};
    bool espCreatureMagic{false};
    bool espCreatureRainbow{false};
    int espSpeciesFilterMode{0};
    std::string espSpeciesFilter;
    bool espCatchableOnly{false};
    bool espShowCaptureChance{false};
    int espMinCapturePercent{0};

    bool espShowHealthText{false};
    bool espShowHealthBar{false};
    int espBoxMode{0};
    bool espBoxFill{false};
    bool espSnaplines{false};
    bool espShinyAlert{false};

    bool hudUidOverrideEnabled{false};
    std::string hudUidOverrideText{"Arkhan"};

    bool eggHasteEnabled{false};
    bool eggHasteEggMovement{false};
    float eggHasteEggSpeedMultiplier{1.50f};
    bool eggHasteDashBoost{false};
    float eggHasteDashMultiplier{1.50f};
    bool eggHasteNoDashCooldown{false};
    bool eggHasteFastCarryBoost{false};
    float eggHasteFastCarryRatio{1.40f};
    bool eggHasteEspEnabled{false};
    int eggHasteEspRange{200};
    bool eggHasteEspEggs{false};
    bool eggHasteEspNests{false};
    bool eggHasteEspResources{false};
    bool eggHasteEspLoot{false};
    bool eggHasteEspBags{false};
    bool eggHasteEspTransfers{false};
    bool eggHasteEspBreakables{false};
    std::uint32_t eggHasteQualityMask{0x7fu};
    bool eggHasteRemoveViewFog{false};
    bool eggHasteRevealFullMap{false};

    bool autofarmEnabled{false};
    bool autofarmIncludeChests{false};
    bool autofarmIncludeAmber{false};
    int autofarmScanRadius{500};
    int autofarmEmptyConfirmRadius{180};
    std::uint32_t autofarmTeleportDelayMs{900};
    std::uint32_t autofarmSettleDelayMs{1500};
    std::uint32_t autofarmResetRequest{0};

    bool autoquestEnabled{false};
    int autoquestSelectedQuestId{};
    std::uint32_t autoquestTeleportRequest{};
    int autoquestSelectedTemplateId{};
    int autoquestSelectedMarkId{};
    int autoquestSelectedMarkRealSceneId{};

    bool mobVacuumEnabled{false};
    int mobVacuumRadius{100};
    int mobVacuumMaxMobs{12};
    float mobVacuumDistance{4.0f};
    float mobVacuumRepositionTolerance{1.5f};
    bool mobVacuumStun{false};
};

class NativeOverlayBridge final {
public:
    static NativeOverlayBridge& Instance();

    bool Start(const std::filesystem::path& arkhanRoot);
    void Stop();
    bool Loaded() const;
    bool Ready() const;
    bool MenuOpen() const;
    NativeUiControls ReadControls() const;
    void UpdateLuaSnapshot(const std::string& protocol);
    void ClearLuaSnapshot();
    std::string StatusText() const;

private:
    NativeOverlayBridge() = default;
    ~NativeOverlayBridge() = default;
    NativeOverlayBridge(const NativeOverlayBridge&) = delete;
    NativeOverlayBridge& operator=(const NativeOverlayBridge&) = delete;

    static void ARKHAN_OVERLAY_CALL HostLog(const char* category, const char* message);
    bool ResolveExportsLocked(HMODULE module);
    void ResetLocked();

    mutable std::mutex mutex_;
    HMODULE module_{};
    ArkhanOverlayStartFn start_{};
    ArkhanOverlayStopFn stop_{};
    ArkhanOverlayBoolFn ready_{};
    ArkhanOverlayBoolFn menuOpen_{};
    ArkhanOverlayGetControlsFn getControls_{};
    ArkhanOverlayGetEspVisualControlsFn getEspVisualControls_{};
    ArkhanOverlayGetUidVisualControlsFn getUidVisualControls_{};
    ArkhanOverlayGetEggHasteAuxControlsFn getEggHasteAuxControls_{};
    ArkhanOverlayGetEggHasteQualityControlsFn getEggHasteQualityControls_{};
    ArkhanOverlayGetPlayerGodModeControlsFn getPlayerGodModeControls_{};
    ArkhanOverlayGetOverloadReliefControlsFn getOverloadReliefControls_{};
    ArkhanOverlayGetInfiUltimateControlsFn getInfiUltimateControls_{};
    ArkhanOverlayGetCaptureBoostControlsFn getCaptureBoostControls_{};
    ArkhanOverlayGetAutofarmControlsFn getAutofarmControls_{};
    ArkhanOverlayGetAutoquestControlsFn getAutoquestControls_{};
    ArkhanOverlayGetAutoquestRouteControlsFn getAutoquestRouteControls_{};
    ArkhanOverlayGetMobVacuumControlsFn getMobVacuumControls_{};
    ArkhanOverlayGetMobVacuumMotionControlsFn getMobVacuumMotionControls_{};
    ArkhanOverlayGetMobVacuumModeControlsFn getMobVacuumModeControls_{};
    ArkhanOverlayUpdateSnapshotFn updateSnapshot_{};
    ArkhanOverlayClearSnapshotFn clearSnapshot_{};
    ArkhanOverlayGetStatusFn getStatus_{};
    std::string status_{"overlay module not loaded"};
};

} // namespace arkhan
