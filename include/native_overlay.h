#pragma once

#include <Windows.h>
#include <unknwn.h>  // IUnknown used by IDXGISwapChain3::ResizeBuffers1
#include <dxgiformat.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct IDXGISwapChain;
struct IDXGISwapChain1;
struct IDXGISwapChain3;
struct DXGI_PRESENT_PARAMETERS;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandList;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;
struct ID3D12DescriptorHeap;
struct ID3D12Resource;
struct ID3D12Fence;

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
    int espBoxMode{0}; // 0 off, 1 full, 2 corners
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
    bool eggHasteQualityWhite{true};
    bool eggHasteQualityGreen{true};
    bool eggHasteQualityBlue{true};
    bool eggHasteQualityPurple{true};
    bool eggHasteQualityGold{true};
    bool eggHasteQualityRainbow{true};
    bool eggHasteQualityUnknown{true};
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
    int autoquestSelectedQuestId{0};
    std::uint32_t autoquestTeleportRequest{0};
    int autoquestSelectedTemplateId{0};
    int autoquestSelectedMarkId{0};
    int autoquestSelectedMarkRealSceneId{0};

    bool mobVacuumEnabled{false};
    int mobVacuumRadius{100};
    int mobVacuumMaxMobs{12};
    float mobVacuumDistance{4.0f};
    float mobVacuumRepositionTolerance{1.5f};
    bool mobVacuumStun{false};
};

struct NativeEspItem {
    std::string kind;
    float screenX{};
    float screenY{};
    float depth{};
    float distance{};
    std::string name;
    int level{};
    std::uint32_t labelMask{};
    std::string actorId;
    float captureChance{-1.0f};
    bool catchable{false};
    float curHp{-1.0f};
    float maxHp{-1.0f};
    float topScreenX{-1.0f};
    float topScreenY{-1.0f};
    float topDepth{-1.0f};
    int quality{};
    std::string value;
};

struct NativeAutomationStats {
    int actions{};
    int requests{};
    int rejected{};
    int positionRejected{};
    int probes{};
    float lastSuccessDistance{};
    float maxSuccessDistance{};
    float maxLearnedExtra{};
    int profiles{};
    int targets{};
    int suppressedBubbles{};
    std::string last;
};

struct NativeCaptureAssistantStats {
    bool enabled{false};
    bool autoThrow{false};
    int mode{0};
    std::string state{"idle"};
    std::string targetActorId{"0"};
    std::string targetName;
    float distance{};
    float chance{-1.0f};
    int itemId{};
    int throws{};
    int errors{};
    std::string last{"idle"};
};


struct NativeEggHasteStats {
    bool bridgeSeen{false};
    bool liveSpace{false};
};

struct NativeInterconnectionStats {
    bool active{};
    bool controlling{};
    std::string petId{"0"};
    std::string templateId{"0"};
    int abilities{};
    int castRewrites{};
    int castRpcSeen{};
    int buffRequests{};
    int errors{};
    float infiUltimateCooldownRate{-1.0f};
    float infiUltimateCooldownMax{-1.0f};
    float infiUltimateSp{-1.0f};
    float infiUltimateSpMax{-1.0f};
    std::string last{"idle"};
};

struct NativeAutofarmStats {
    bool bridgeSeen{false};
    bool enabled{false};
    bool dataLoaded{false};
    std::string state{"idle"};
    int selectedTotal{};
    int remaining{};
    int collected{};
    int absent{};
    int deferred{};
    int liveNearby{};
    int currentIndex{};
    std::string currentKind{"none"};
    std::string currentId{"0"};
    float targetDistance{-1.0f};
    int teleports{};
    int interactions{};
    int errors{};
    std::string ySource{"none"};
    std::string last{"waiting"};
};

struct NativeQuestEntry {
    int id{};
    int state{};
    std::string title;
};

struct NativeQuestAniimoEntry {
    int templateId{};
    bool missing{false};
    std::string name;
};

struct NativeQuestHabitatEntry {
    int templateId{};
    int smallAreaId{};
    std::string areaName;
    std::string condition;
    int markId{};
    int markRealSceneId{};
    int teleportSceneId{};
    std::string markName;
    bool areaFallback{false};
};

struct NativeAutoquestStats {
    bool bridgeSeen{false};
    bool enabled{false};
    int activeQuestCount{};
    int selectedQuestId{};
    std::string selectedTitle;
    std::string objective;
    std::string state{"waiting"};
    float targetDistance{-1.0f};
    std::vector<NativeQuestEntry> quests;
    int petFormsOwned{};
    int petFormsRequired{};
    std::vector<NativeQuestAniimoEntry> petForms;
    std::vector<NativeQuestHabitatEntry> habitats;
};

struct NativeMobVacuumStats {
    bool bridgeSeen{false};
    bool enabled{false};
    int radius{100};
    int limit{12};
    int candidates{};
    int active{};
    int moved{};
    int errors{};
    float holdDistance{4.0f};
    std::string last{"waiting"};
};

struct NativeEspSnapshot {
    float sourceWidth{1920.0f};
    float sourceHeight{1080.0f};
    std::vector<NativeEspItem> items;
    NativeAutomationStats collectStats;
    NativeAutomationStats chestStats;
    NativeCaptureAssistantStats captureStats;
    NativeEggHasteStats eggHasteStats;
    NativeInterconnectionStats interconnectionStats;
    NativeAutofarmStats autofarmStats;
    NativeAutoquestStats autoquestStats;
    NativeMobVacuumStats mobVacuumStats;
    std::string status;
};

enum class OverlayRendererBackend : std::uint8_t {
    None,
    D3D11,
    D3D12,
};

class NativeOverlay final {
public:
    static NativeOverlay& Instance();

    bool Start();
    void Stop();
    bool Ready() const;
    bool MenuOpen() const;
    NativeUiControls ReadControls() const;
    void UpdateLuaSnapshot(const std::string& protocol);
    void ClearLuaSnapshot();
    std::string StatusText() const;

private:
    struct Dx12FrameContext {
        ID3D12CommandAllocator* allocator{};
        ID3D12Resource* backBuffer{};
        std::uintptr_t rtvHandle{};
        std::uint64_t fenceValue{};
    };

    NativeOverlay() = default;
    ~NativeOverlay() = default;
    NativeOverlay(const NativeOverlay&) = delete;
    NativeOverlay& operator=(const NativeOverlay&) = delete;

    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ResizeBuffers1Fn = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                                 const UINT*, IUnknown* const*);
    using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    static HRESULT __stdcall PresentHookDx11(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    static HRESULT __stdcall PresentHookDx12(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    static HRESULT __stdcall Present1HookDx12(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                               const DXGI_PRESENT_PARAMETERS* parameters);
    static HRESULT __stdcall ResizeBuffersHookDx11(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                                    UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags);
    static HRESULT __stdcall ResizeBuffersHookDx12(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                                    UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags);
    static HRESULT __stdcall ResizeBuffers1HookDx12(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width,
                                                     UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags,
                                                     const UINT* creationNodeMask,
                                                     IUnknown* const* presentQueues);
    static void __stdcall ExecuteCommandListsHook(ID3D12CommandQueue* queue, UINT numCommandLists,
                                                   ID3D12CommandList* const* commandLists);
    static LRESULT CALLBACK WindowProcHook(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool InstallSwapChainHooks();
    void RemoveSwapChainHooks();
    bool InstallHook(void* target, void* detour, void** original, const char* name);
    static OverlayRendererBackend DetectBackend(IDXGISwapChain* swapChain);
    bool InitializeImGui(IDXGISwapChain* swapChain, OverlayRendererBackend backend);
    bool InitializeCommonImGui(HWND gameWindow);
    bool InitializeDx11(IDXGISwapChain* swapChain);
    bool InitializeDx12(IDXGISwapChain* swapChain);
    bool InitializeBackgroundTexture();
    bool CreateDx11BackgroundTexture(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);
    bool CreateDx12BackgroundTexture(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);
    void ShutdownImGui();

    bool CreateDx11RenderTarget(IDXGISwapChain* swapChain);
    void ReleaseDx11RenderTarget();
    bool CreateDx12Resources(IDXGISwapChain* swapChain);
    void ReleaseDx12Resources(bool waitForGpu);
    bool RenderDx12DrawData(IDXGISwapChain* swapChain);
    void WaitForDx12Gpu();
    void WaitForDx12Frame(Dx12FrameContext& frame);
    void CaptureDx12CommandQueue(ID3D12CommandQueue* queue);
    ID3D12CommandQueue* AcquireCapturedDx12Queue(ID3D12Device* device);
    void ReleaseCapturedDx12Queue();

    void OnPresent(IDXGISwapChain* swapChain, OverlayRendererBackend backend);
    void OnBeforeResize(IDXGISwapChain* swapChain, OverlayRendererBackend backend);
    void OnAfterResize(IDXGISwapChain* swapChain, OverlayRendererBackend backend);
    void RenderMenu();
    void RenderEsp();
    void PollMobVacuumHotkey();
    void DisableAll();
    void ToggleMenu();
    bool EnsureWindowProcHook();
    void EnforceInputOwnership();
    std::shared_ptr<const NativeEspSnapshot> ReadSnapshot() const;
    const char* RendererName() const;

    mutable std::mutex controlsMutex_;
    NativeUiControls controls_;
    mutable std::mutex snapshotMutex_;
    std::shared_ptr<const NativeEspSnapshot> snapshot_{std::make_shared<NativeEspSnapshot>()};
    mutable std::mutex statusMutex_;
    mutable std::recursive_mutex imguiMutex_;
    mutable std::mutex dx12QueueMutex_;
    std::string status_{"overlay waiting"};

    std::atomic<bool> started_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> menuOpen_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> dx12QueueLocked_{false};

    std::atomic<int> mobVacuumHotkeyVk_{0};
    std::atomic<bool> mobVacuumHotkeyCapture_{false};
    std::atomic<bool> mobVacuumHotkeyWasDown_{false};

    std::string selectedProfile_;
    std::string profileStatus_;

    void* presentTargetDx11_{};
    void* resizeTargetDx11_{};
    PresentFn originalPresentDx11_{};
    ResizeBuffersFn originalResizeDx11_{};
    void* presentTargetDx12_{};
    void* present1TargetDx12_{};
    void* resizeTargetDx12_{};
    void* resize1TargetDx12_{};
    PresentFn originalPresentDx12_{};
    Present1Fn originalPresent1Dx12_{};
    ResizeBuffersFn originalResizeDx12_{};
    ResizeBuffers1Fn originalResize1Dx12_{};
    void* executeCommandListsTarget_{};
    ExecuteCommandListsFn originalExecuteCommandLists_{};

    IDXGISwapChain* activeSwapChain_{};
    OverlayRendererBackend rendererBackend_{OverlayRendererBackend::None};

    ID3D11Device* dx11Device_{};
    ID3D11DeviceContext* dx11Context_{};
    ID3D11RenderTargetView* dx11RenderTarget_{};
    ID3D11ShaderResourceView* dx11BackgroundView_{};
    ID3D11Texture2D* dx11BackgroundTexture_{};

    ID3D12Device* dx12Device_{};
    ID3D12CommandQueue* dx12CommandQueue_{};
    ID3D12CommandQueue* capturedDx12CommandQueue_{};
    ID3D12DescriptorHeap* dx12RtvHeap_{};
    ID3D12DescriptorHeap* dx12SrvHeap_{};
    ID3D12Resource* dx12BackgroundTexture_{};
    ID3D12GraphicsCommandList* dx12CommandList_{};
    ID3D12Fence* dx12Fence_{};
    HANDLE dx12FenceEvent_{};
    std::vector<Dx12FrameContext> dx12Frames_;
    std::uint32_t dx12RtvDescriptorSize_{};
    std::uint32_t dx12SrvDescriptorSize_{};
    std::uint64_t dx12NextFenceValue_{1};
    std::uint64_t backgroundTextureId_{};
    DXGI_FORMAT dx12RtvFormat_{DXGI_FORMAT_UNKNOWN};

    HWND gameWindow_{};
    WNDPROC originalWindowProc_{};
    std::atomic<bool> inputOwned_{false};
    bool imguiContextCreated_{false};
    bool imguiWin32Ready_{false};
    bool imguiDx11Ready_{false};
    bool imguiDx12Ready_{false};
    bool minHookInitialized_{false};

};

} // namespace arkhan
