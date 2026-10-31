#include "native_overlay.h"

#include "overlay_runtime.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <windowsx.h>

#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace arkhan {
namespace {

constexpr int kArkhanBackgroundResourceId = 201;
constexpr float kArkhanBackgroundAspect = 1672.0f / 944.0f;

template <typename T>
void ReleaseCom(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::vector<std::string_view> SplitTabs(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto next = line.find('\t', start);
        if (next == std::string_view::npos) {
            result.emplace_back(line.substr(start));
            break;
        }
        result.emplace_back(line.substr(start, next - start));
        start = next + 1;
    }
    return result;
}

float ParseFloat(std::string_view value, float fallback = 0.0f) {
    std::string buffer(value);
    char* end = nullptr;
    const float parsed = std::strtof(buffer.c_str(), &end);
    return end && end != buffer.c_str() ? parsed : fallback;
}

int ParseInt(std::string_view value, int fallback = 0) {
    std::string buffer(value);
    char* end = nullptr;
    const long parsed = std::strtol(buffer.c_str(), &end, 10);
    return end && end != buffer.c_str() ? static_cast<int>(parsed) : fallback;
}

LRESULT CALLBACK DummyWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool IsMouseMessage(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

bool IsKeyboardMessage(UINT message) {
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_CHAR || message == WM_SYSCHAR;
}

std::string VirtualKeyLabel(int virtualKey) {
    if (virtualKey <= 0) return "None";
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24)
        return "F" + std::to_string(virtualKey - VK_F1 + 1);
    if ((virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z'))
        return std::string(1, static_cast<char>(virtualKey));
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
        return "Num " + std::to_string(virtualKey - VK_NUMPAD0);

    UINT scanCode = MapVirtualKeyA(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    LONG keyInfo = static_cast<LONG>(scanCode << 16);
    switch (virtualKey) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
        case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
            keyInfo |= (1 << 24);
            break;
        default:
            break;
    }
    char name[64]{};
    if (GetKeyNameTextA(keyInfo, name, static_cast<int>(sizeof(name))) > 0) return name;
    char fallback[16]{};
    std::snprintf(fallback, sizeof(fallback), "VK_%02X", virtualKey & 0xFF);
    return fallback;
}

bool RawInputContainsKeyboardKey(LPARAM lParam, int virtualKey) {
    if (virtualKey <= 0) return false;
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || size < sizeof(RAWINPUTHEADER)) {
        return false;
    }

    std::vector<std::uint8_t> storage(size);
    const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, storage.data(), &size,
                                      sizeof(RAWINPUTHEADER));
    if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER)) return false;
    const auto* raw = reinterpret_cast<const RAWINPUT*>(storage.data());
    return raw->header.dwType == RIM_TYPEKEYBOARD &&
           static_cast<int>(raw->data.keyboard.VKey) == virtualKey;
}

bool RawInputContainsMouse4(LPARAM lParam) {
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || size < sizeof(RAWINPUTHEADER)) {
        return false;
    }

    std::vector<std::uint8_t> storage(size);
    const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, storage.data(), &size,
                                      sizeof(RAWINPUTHEADER));
    if (read == static_cast<UINT>(-1) || read < sizeof(RAWINPUTHEADER)) return false;

    const auto* raw = reinterpret_cast<const RAWINPUT*>(storage.data());
    if (raw->header.dwType != RIM_TYPEMOUSE) return false;
    const USHORT flags = raw->data.mouse.usButtonFlags;
    return (flags & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP)) != 0;
}

ImU32 WithAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w = std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

void DrawCornerBox(ImDrawList* draw, const ImVec2& min, const ImVec2& max, ImU32 color, float thickness) {
    if (!draw) return;
    const float width = std::max(0.0f, max.x - min.x);
    const float height = std::max(0.0f, max.y - min.y);
    const float cornerX = std::clamp(width * 0.28f, 5.0f, 22.0f);
    const float cornerY = std::clamp(height * 0.22f, 5.0f, 28.0f);
    draw->AddLine(min, ImVec2(min.x + cornerX, min.y), color, thickness);
    draw->AddLine(min, ImVec2(min.x, min.y + cornerY), color, thickness);
    draw->AddLine(ImVec2(max.x - cornerX, min.y), ImVec2(max.x, min.y), color, thickness);
    draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + cornerY), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y - cornerY), ImVec2(min.x, max.y), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + cornerX, max.y), color, thickness);
    draw->AddLine(ImVec2(max.x - cornerX, max.y), max, color, thickness);
    draw->AddLine(ImVec2(max.x, max.y - cornerY), max, color, thickness);
}

ImU32 EspColor(std::string_view kind) {
    if (kind == "collect-item") return IM_COL32(110, 226, 132, 245);
    if (kind == "chest") return IM_COL32(241, 192, 82, 245);
    if (kind == "enemy-puppet") return IM_COL32(244, 105, 105, 245);
    if (kind == "puppet" || kind == "pet" || kind == "player-pet") return IM_COL32(113, 190, 247, 245);
    if (kind == "npc") return IM_COL32(203, 151, 247, 245);
    if (kind == "egg-haste-egg") return IM_COL32(255, 224, 128, 250);
    if (kind == "egg-haste-nest") return IM_COL32(255, 178, 96, 250);
    if (kind == "egg-haste-resource") return IM_COL32(112, 214, 164, 250);
    if (kind == "egg-haste-loot") return IM_COL32(120, 205, 255, 250);
    if (kind == "egg-haste-bag") return IM_COL32(220, 150, 255, 250);
    if (kind == "egg-haste-transfer") return IM_COL32(255, 120, 170, 250);
    if (kind == "egg-haste-breakable") return IM_COL32(230, 120, 100, 250);
    return IM_COL32(224, 224, 224, 240);
}

ImU32 EggChestColor(int quality) {
    if (quality == 5) return IM_COL32(255, 190, 48, 255);
    if (quality == 4) return IM_COL32(185, 105, 255, 255);
    if (quality == 6) return IM_COL32(255, 120, 195, 255);
    return EspColor("egg-haste-resource");
}

#define ARKHAN_PROFILE_BOOL_FIELDS(X) \
    X(infiniteStamina) X(noFallDamage) X(speedBoost) X(autoCollect) X(autoChest) X(playerGodMode) X(playerOverloadRelief) \
    X(captureAssistant) X(captureAutoThrow) X(captureFastThrow) X(captureSkipCameraTransition) \
    X(autoDialogueSkip) X(autoCutsceneSkip) X(fovOverride) X(interconnectionGodMode) X(interconnectionNoCooldown) \
    X(interconnectionNoCost) X(interconnectionDamage) X(interconnectionAttackSpeed) \
    X(interconnectionAttackFurious) X(interconnectionAttackEnhance) X(interconnectionAttackHunt) \
    X(interconnectionCaptureBoost) X(interconnectionInfiUltimate) \
    X(espEnabled) X(espCollect) X(espChest) X(espEnemy) X(espCreature) X(espNpc) X(espCreatureNormal) \
    X(espCreatureShiny) X(espCreatureBoss) X(espCreatureElite) X(espCreatureVariant) X(espCreatureMagic) \
    X(espCreatureRainbow) X(espCatchableOnly) X(espShowCaptureChance) X(espShowHealthText) X(espShowHealthBar) \
    X(espBoxFill) X(espSnaplines) X(espShinyAlert) X(hudUidOverrideEnabled) X(eggHasteEnabled) X(eggHasteEggMovement) X(eggHasteDashBoost) \
    X(eggHasteNoDashCooldown) X(eggHasteFastCarryBoost) X(eggHasteEspEnabled) X(eggHasteEspEggs) \
    X(eggHasteEspNests) X(eggHasteEspResources) X(eggHasteEspLoot) X(eggHasteEspBags) X(eggHasteEspTransfers) \
    X(eggHasteEspBreakables) X(eggHasteQualityWhite) X(eggHasteQualityGreen) X(eggHasteQualityBlue) \
    X(eggHasteQualityPurple) X(eggHasteQualityGold) X(eggHasteQualityRainbow) X(eggHasteQualityUnknown) \
    X(eggHasteRemoveViewFog) X(eggHasteRevealFullMap) X(autofarmEnabled) X(autoquestEnabled) \
    X(autofarmIncludeChests) X(autofarmIncludeAmber) X(mobVacuumEnabled) X(mobVacuumStun)
#define ARKHAN_PROFILE_INT_FIELDS(X) \
    X(collectRange) X(chestRange) X(captureTargetMode) X(captureRange) X(captureMinPercent) X(fov) \
    X(espRange) X(espSpeciesFilterMode) X(espMinCapturePercent) X(espBoxMode) X(eggHasteEspRange) \
    X(autofarmScanRadius) X(autofarmEmptyConfirmRadius) X(autoquestSelectedQuestId) X(mobVacuumRadius) X(mobVacuumMaxMobs)
#define ARKHAN_PROFILE_UINT_FIELDS(X) X(autofarmTeleportDelayMs) X(autofarmSettleDelayMs)
#define ARKHAN_PROFILE_FLOAT_FIELDS(X) \
    X(speedMultiplier) X(captureThrowAnimSpeed) X(captureBallSpeed) X(captureRecoveryDelay) \
    X(interconnectionDamageMultiplier) X(interconnectionAttackSpeedMultiplier) X(eggHasteEggSpeedMultiplier) \
    X(eggHasteDashMultiplier) X(eggHasteFastCarryRatio) X(mobVacuumDistance) X(mobVacuumRepositionTolerance)
#define ARKHAN_PROFILE_STRING_FIELDS(X) X(captureSpecies) X(espSpeciesFilter) X(hudUidOverrideText)

enum class ProfileFieldType { Boolean, Integer, Unsigned, Float, String };
struct ProfileField { const char* name; ProfileFieldType type; std::size_t offset; };
#define ARKHAN_BOOL_FIELD(name) {#name, ProfileFieldType::Boolean, offsetof(NativeUiControls, name)},
#define ARKHAN_INT_FIELD(name) {#name, ProfileFieldType::Integer, offsetof(NativeUiControls, name)},
#define ARKHAN_UINT_FIELD(name) {#name, ProfileFieldType::Unsigned, offsetof(NativeUiControls, name)},
#define ARKHAN_FLOAT_FIELD(name) {#name, ProfileFieldType::Float, offsetof(NativeUiControls, name)},
#define ARKHAN_STRING_FIELD(name) {#name, ProfileFieldType::String, offsetof(NativeUiControls, name)},
const ProfileField kProfileFields[] = {
    ARKHAN_PROFILE_BOOL_FIELDS(ARKHAN_BOOL_FIELD)
    ARKHAN_PROFILE_INT_FIELDS(ARKHAN_INT_FIELD)
    ARKHAN_PROFILE_UINT_FIELDS(ARKHAN_UINT_FIELD)
    ARKHAN_PROFILE_FLOAT_FIELDS(ARKHAN_FLOAT_FIELD)
    ARKHAN_PROFILE_STRING_FIELDS(ARKHAN_STRING_FIELD)
};
#undef ARKHAN_BOOL_FIELD
#undef ARKHAN_INT_FIELD
#undef ARKHAN_UINT_FIELD
#undef ARKHAN_FLOAT_FIELD
#undef ARKHAN_STRING_FIELD

std::filesystem::path ProfilesDirectory() {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return {};
    return std::filesystem::path(executable.data()).parent_path() / L"Arkhan" / L"profiles";
}

std::string SafeProfileName(std::string_view name) {
    std::string safe;
    safe.reserve(std::min<std::size_t>(name.size(), 48));
    for (unsigned char ch : name) {
        if (safe.size() >= 48) break;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == ' ') safe.push_back(static_cast<char>(ch));
    }
    while (!safe.empty() && safe.front() == ' ') safe.erase(safe.begin());
    while (!safe.empty() && safe.back() == ' ') safe.pop_back();
    return safe;
}

std::vector<std::string> ListProfiles() {
    std::vector<std::string> names;
    std::error_code ec;
    const auto root = ProfilesDirectory();
    if (root.empty() || !std::filesystem::is_directory(root, ec)) return names;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != L".ini") continue;
        names.push_back(it->path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool SaveProfileFile(const std::string& profileName, const NativeUiControls& controls, int vacuumHotkey) {
    const auto safe = SafeProfileName(profileName);
    if (safe.empty()) return false;
    std::error_code ec;
    const auto root = ProfilesDirectory();
    if (root.empty()) return false;
    std::filesystem::create_directories(root, ec);
    if (ec) return false;
    std::ofstream out(root / (safe + ".ini"), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "version=1\n";
    const auto* bytes = reinterpret_cast<const std::byte*>(&controls);
    for (const auto& field : kProfileFields) {
        out << field.name << '=';
        const auto* address = bytes + field.offset;
        switch (field.type) {
            case ProfileFieldType::Boolean: out << (*reinterpret_cast<const bool*>(address) ? 1 : 0); break;
            case ProfileFieldType::Integer: out << *reinterpret_cast<const int*>(address); break;
            case ProfileFieldType::Unsigned: out << *reinterpret_cast<const std::uint32_t*>(address); break;
            case ProfileFieldType::Float: out << std::setprecision(9) << *reinterpret_cast<const float*>(address); break;
            case ProfileFieldType::String: out << std::quoted(*reinterpret_cast<const std::string*>(address)); break;
        }
        out << '\n';
    }
    out << "hotkeyVacuum=" << vacuumHotkey << '\n';
    return out.good();
}

bool LoadProfileFile(const std::string& profileName, NativeUiControls& controls, int& vacuumHotkey) {
    const auto safe = SafeProfileName(profileName);
    if (safe.empty()) return false;
    std::ifstream in(ProfilesDirectory() / (safe + ".ini"), std::ios::binary);
    if (!in) return false;
    for (std::string line; std::getline(in, line);) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        if (key == "hotkeyVacuum") {
            vacuumHotkey = std::clamp(std::atoi(value.c_str()), 0, 255);
            continue;
        }
        const auto* field = std::find_if(std::begin(kProfileFields), std::end(kProfileFields),
                                         [&](const ProfileField& item) { return key == item.name; });
        if (field == std::end(kProfileFields)) continue;
        auto* address = reinterpret_cast<std::byte*>(&controls) + field->offset;
        try {
            switch (field->type) {
                case ProfileFieldType::Boolean: *reinterpret_cast<bool*>(address) = std::atoi(value.c_str()) != 0; break;
                case ProfileFieldType::Integer: *reinterpret_cast<int*>(address) = std::stoi(value); break;
                case ProfileFieldType::Unsigned: *reinterpret_cast<std::uint32_t*>(address) = static_cast<std::uint32_t>(std::stoul(value)); break;
                case ProfileFieldType::Float: *reinterpret_cast<float*>(address) = std::stof(value); break;
                case ProfileFieldType::String: {
                    std::istringstream stream(value);
                    stream >> std::quoted(*reinterpret_cast<std::string*>(address));
                    break;
                }
            }
        } catch (...) {}
    }
    controls.autofarmResetRequest = 0;
    return true;
}

constexpr std::uint32_t kPetLabelShiny = 1u;
constexpr std::uint32_t kPetLabelBoss = 2u;
constexpr std::uint32_t kPetLabelElite = 4u;
constexpr std::uint32_t kPetLabelVariant = 8u;
constexpr std::uint32_t kPetLabelMagic = 16u;
constexpr std::uint32_t kPetLabelRainbow = 32u;
constexpr std::uint32_t kKnownPetLabelMask = kPetLabelShiny | kPetLabelBoss | kPetLabelElite |
                                               kPetLabelVariant | kPetLabelMagic | kPetLabelRainbow;

bool IsCreatureKind(std::string_view kind) {
    return kind == "enemy-puppet" || kind == "puppet" || kind == "pet" || kind == "player-pet";
}

bool CreatureLabelEnabled(const NativeUiControls& controls, std::uint32_t labelMask) {
    const std::uint32_t known = labelMask & kKnownPetLabelMask;
    if (known == 0u) return controls.espCreatureNormal;

    return ((known & kPetLabelShiny) != 0u && controls.espCreatureShiny) ||
           ((known & kPetLabelBoss) != 0u && controls.espCreatureBoss) ||
           ((known & kPetLabelElite) != 0u && controls.espCreatureElite) ||
           ((known & kPetLabelVariant) != 0u && controls.espCreatureVariant) ||
           ((known & kPetLabelMagic) != 0u && controls.espCreatureMagic) ||
           ((known & kPetLabelRainbow) != 0u && controls.espCreatureRainbow);
}

bool IsEggHasteKind(std::string_view kind) {
    return kind.rfind("egg-haste-", 0) == 0;
}

bool EggHasteKindEnabled(const NativeUiControls& controls, std::string_view kind) {
    if (kind == "egg-haste-egg") return controls.eggHasteEspEggs;
    if (kind == "egg-haste-nest") return controls.eggHasteEspNests;
    if (kind == "egg-haste-resource") return controls.eggHasteEspResources;
    if (kind == "egg-haste-loot") return controls.eggHasteEspLoot;
    if (kind == "egg-haste-bag") return controls.eggHasteEspBags;
    if (kind == "egg-haste-transfer") return controls.eggHasteEspTransfers;
    if (kind == "egg-haste-breakable") return controls.eggHasteEspBreakables;
    return false;
}

bool KindEnabled(const NativeUiControls& controls, std::string_view kind) {
    if (IsEggHasteKind(kind)) return false;
    if (kind == "collect-item") return controls.espCollect;
    if (kind == "chest") return controls.espChest;
    if (kind == "enemy-puppet") return controls.espEnemy;
    if (IsCreatureKind(kind)) return controls.espCreature;
    if (kind == "npc") return controls.espNpc;
    return true;
}

const char* MhStatusName(MH_STATUS status) {
    const char* text = MH_StatusToString(status);
    return text ? text : "unknown";
}

} // namespace

NativeOverlay& NativeOverlay::Instance() {
    static NativeOverlay overlay;
    return overlay;
}

bool NativeOverlay::Start() {
    if (started_.exchange(true)) return true;
    stopping_.store(false);
    menuOpen_.store(false);

    if (!InstallSwapChainHooks()) {
        started_.store(false);
        std::lock_guard lock(statusMutex_);
        status_ = "Dear ImGui / MinHook install failed";
        OverlayLog("overlay", "ARKHAN_OVERLAY_HOOK_FAILED backend=DearImGui hook=MinHook");
        return false;
    }

    {
        std::lock_guard lock(statusMutex_);
        status_ = "MinHook installed; waiting for game DX11/DX12 swapchain";
    }
    OverlayLog("overlay", "ARKHAN_OVERLAY_HOOK_READY backend=DearImGui+D3D11/D3D12 hook=MinHook key=Mouse4");
    return true;
}

void NativeOverlay::Stop() {
    if (!started_.exchange(false)) return;
    stopping_.store(true);
    RemoveSwapChainHooks();
    ShutdownImGui();
    ready_.store(false);
    menuOpen_.store(false);
    ClearLuaSnapshot();
    {
        std::lock_guard lock(statusMutex_);
        status_ = "overlay stopped";
    }
    OverlayLog("overlay", "ARKHAN_OVERLAY_STOPPED backend=DearImGui hook=MinHook");
}

bool NativeOverlay::Ready() const { return ready_.load(); }
bool NativeOverlay::MenuOpen() const { return menuOpen_.load(); }

NativeUiControls NativeOverlay::ReadControls() const {
    std::lock_guard lock(controlsMutex_);
    return controls_;
}

std::string NativeOverlay::StatusText() const {
    std::lock_guard lock(statusMutex_);
    return status_;
}

void NativeOverlay::ClearLuaSnapshot() {
    std::lock_guard lock(snapshotMutex_);
    snapshot_ = std::make_shared<NativeEspSnapshot>();
}

std::shared_ptr<const NativeEspSnapshot> NativeOverlay::ReadSnapshot() const {
    std::lock_guard lock(snapshotMutex_);
    return snapshot_;
}

void NativeOverlay::UpdateLuaSnapshot(const std::string& protocol) {
    NativeEspSnapshot next;
    std::size_t start = 0;
    bool metaSeen = false;
    while (start <= protocol.size()) {
        const auto end = protocol.find('\n', start);
        const std::string_view line(protocol.data() + start,
                                    (end == std::string::npos ? protocol.size() : end) - start);
        if (!line.empty()) {
            auto fields = SplitTabs(line);
            if (!fields.empty() && fields[0] == "META" && fields.size() >= 4) {
                next.sourceWidth = std::max(1.0f, ParseFloat(fields[1], 1920.0f));
                next.sourceHeight = std::max(1.0f, ParseFloat(fields[2], 1080.0f));
                if (fields.size() >= 5) next.status = std::string(fields[4]);
                metaSeen = true;
            } else if (!fields.empty() && fields[0] == "F" && fields.size() >= 14) {
                NativeAutomationStats stats;
                stats.actions = ParseInt(fields[2]);
                stats.requests = ParseInt(fields[3]);
                stats.rejected = ParseInt(fields[4]);
                stats.positionRejected = ParseInt(fields[5]);
                stats.probes = ParseInt(fields[6]);
                stats.lastSuccessDistance = ParseFloat(fields[7]);
                stats.maxSuccessDistance = ParseFloat(fields[8]);
                stats.maxLearnedExtra = ParseFloat(fields[9]);
                stats.profiles = ParseInt(fields[10]);
                stats.targets = ParseInt(fields[11]);
                stats.suppressedBubbles = ParseInt(fields[12]);
                stats.last = std::string(fields[13]);
                if (fields[1] == "autoCollect") next.collectStats = std::move(stats);
                else if (fields[1] == "autoChest") next.chestStats = std::move(stats);
            } else if (!fields.empty() && fields[0] == "I" && fields.size() >= 11) {
                auto& stats = next.interconnectionStats;
                stats.active = ParseInt(fields[1]) != 0;
                stats.controlling = ParseInt(fields[2]) != 0;
                stats.petId = std::string(fields[3]);
                stats.templateId = std::string(fields[4]);
                stats.abilities = ParseInt(fields[5]);
                stats.castRewrites = ParseInt(fields[6]);
                stats.castRpcSeen = ParseInt(fields[7]);
                stats.buffRequests = ParseInt(fields[8]);
                stats.errors = ParseInt(fields[9]);
                stats.last = std::string(fields[10]);
                if (fields.size() >= 15) {
                    stats.infiUltimateCooldownRate = ParseFloat(fields[11], -1.0f);
                    stats.infiUltimateCooldownMax = ParseFloat(fields[12], -1.0f);
                    stats.infiUltimateSp = ParseFloat(fields[13], -1.0f);
                    stats.infiUltimateSpMax = ParseFloat(fields[14], -1.0f);
                }
            } else if (!fields.empty() && fields[0] == "H" && fields.size() >= 2) {
                next.eggHasteStats.bridgeSeen = true;
                next.eggHasteStats.liveSpace = ParseInt(fields[1]) != 0;
            } else if (!fields.empty() && fields[0] == "C" && fields.size() >= 13) {
                auto& stats = next.captureStats;
                stats.enabled = ParseInt(fields[1]) != 0;
                stats.autoThrow = ParseInt(fields[2]) != 0;
                stats.mode = ParseInt(fields[3]);
                stats.state = std::string(fields[4]);
                stats.targetActorId = std::string(fields[5]);
                stats.targetName = std::string(fields[6]);
                stats.distance = ParseFloat(fields[7]);
                stats.chance = ParseFloat(fields[8], -1.0f);
                stats.itemId = ParseInt(fields[9]);
                stats.throws = ParseInt(fields[10]);
                stats.errors = ParseInt(fields[11]);
                stats.last = std::string(fields[12]);
            } else if (!fields.empty() && fields[0] == "AF" && fields.size() >= 18) {
                auto& stats = next.autofarmStats;
                stats.bridgeSeen = true;
                stats.enabled = ParseInt(fields[1]) != 0;
                stats.dataLoaded = ParseInt(fields[2]) != 0;
                stats.state = std::string(fields[3]);
                stats.selectedTotal = ParseInt(fields[4]);
                stats.remaining = ParseInt(fields[5]);
                stats.collected = ParseInt(fields[6]);
                stats.absent = ParseInt(fields[7]);
                stats.deferred = ParseInt(fields[8]);
                stats.liveNearby = ParseInt(fields[9]);
                stats.currentIndex = ParseInt(fields[10]);
                stats.currentKind = std::string(fields[11]);
                stats.currentId = std::string(fields[12]);
                stats.targetDistance = ParseFloat(fields[13], -1.0f);
                stats.teleports = ParseInt(fields[14]);
                stats.interactions = ParseInt(fields[15]);
                stats.errors = ParseInt(fields[16]);
                stats.ySource = std::string(fields[17]);
                if (fields.size() >= 19) stats.last = std::string(fields[18]);
            } else if (!fields.empty() && fields[0] == "AQ" && fields.size() >= 9) {
                auto& stats = next.autoquestStats;
                stats.bridgeSeen = true;
                stats.enabled = ParseInt(fields[1]) != 0;
                stats.activeQuestCount = ParseInt(fields[2]);
                stats.selectedQuestId = ParseInt(fields[3]);
                stats.selectedTitle = std::string(fields[4]);
                stats.objective = std::string(fields[5]);
                stats.state = std::string(fields[6]);
                stats.targetDistance = ParseFloat(fields[7], -1.0f);
                if (fields.size() >= 11) {
                    stats.petFormsOwned = ParseInt(fields[9]);
                    stats.petFormsRequired = ParseInt(fields[10]);
                }
            } else if (!fields.empty() && fields[0] == "AQL" && fields.size() >= 4) {
                NativeQuestEntry quest;
                quest.id = ParseInt(fields[1]);
                quest.state = ParseInt(fields[2]);
                quest.title = std::string(fields[3]);
                next.autoquestStats.quests.emplace_back(std::move(quest));
            } else if (!fields.empty() && fields[0] == "AQF" && fields.size() >= 4) {
                NativeQuestAniimoEntry pet;
                pet.templateId = ParseInt(fields[1]);
                pet.missing = ParseInt(fields[2]) != 0;
                pet.name = std::string(fields[3]);
                next.autoquestStats.petForms.emplace_back(std::move(pet));
            } else if (!fields.empty() && fields[0] == "AQH" && fields.size() >= 10) {
                NativeQuestHabitatEntry habitat;
                habitat.templateId = ParseInt(fields[1]);
                habitat.smallAreaId = ParseInt(fields[2]);
                habitat.areaName = std::string(fields[3]);
                habitat.condition = std::string(fields[4]);
                habitat.markId = ParseInt(fields[5]);
                habitat.markRealSceneId = ParseInt(fields[6]);
                habitat.teleportSceneId = ParseInt(fields[7]);
                habitat.markName = std::string(fields[8]);
                habitat.areaFallback = ParseInt(fields[9]) != 0;
                next.autoquestStats.habitats.emplace_back(std::move(habitat));
            } else if (!fields.empty() && fields[0] == "MV" && fields.size() >= 10) {
                auto& stats = next.mobVacuumStats;
                stats.bridgeSeen = true;
                stats.enabled = ParseInt(fields[1]) != 0;
                stats.radius = ParseInt(fields[2], 100);
                stats.limit = ParseInt(fields[3], 12);
                stats.holdDistance = ParseFloat(fields[4], 4.0f);
                stats.candidates = ParseInt(fields[5]);
                stats.active = ParseInt(fields[6]);
                stats.moved = ParseInt(fields[7]);
                stats.errors = ParseInt(fields[8]);
                stats.last = std::string(fields[9]);
            } else if (!fields.empty() && fields[0] == "E" && fields.size() >= 8) {
                NativeEspItem item;
                item.kind = std::string(fields[1]);
                item.screenX = ParseFloat(fields[2]);
                item.screenY = ParseFloat(fields[3]);
                item.depth = ParseFloat(fields[4]);
                item.distance = ParseFloat(fields[5]);
                item.name = std::string(fields[6]);
                item.level = ParseInt(fields[7]);
                if (fields.size() >= 9) item.labelMask = static_cast<std::uint32_t>(std::max(0, ParseInt(fields[8])));
                if (fields.size() >= 10) item.actorId = std::string(fields[9]);
                if (fields.size() >= 11) item.captureChance = ParseFloat(fields[10], -1.0f);
                if (fields.size() >= 12) item.catchable = ParseInt(fields[11]) != 0;
                if (fields.size() >= 13) item.curHp = ParseFloat(fields[12], -1.0f);
                if (fields.size() >= 14) item.maxHp = ParseFloat(fields[13], -1.0f);
                if (fields.size() >= 15) item.topScreenX = ParseFloat(fields[14], -1.0f);
                if (fields.size() >= 16) item.topScreenY = ParseFloat(fields[15], -1.0f);
                if (fields.size() >= 17) item.topDepth = ParseFloat(fields[16], -1.0f);
                if (fields.size() >= 18) item.quality = ParseInt(fields[17]);
                if (fields.size() >= 19) item.value = std::string(fields[18]);
                next.items.emplace_back(std::move(item));
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!metaSeen) {
        next.status = protocol.rfind("OK|", 0) == 0 ? "unexpected OK wrapper" : "invalid ESP protocol";
    }
    std::lock_guard lock(snapshotMutex_);
    snapshot_ = std::make_shared<NativeEspSnapshot>(std::move(next));
}

bool NativeOverlay::InstallHook(void* target, void* detour, void** original, const char* name) {
    if (!target || !detour || !original) return false;
    const MH_STATUS create = MH_CreateHook(target, detour, original);
    if (create != MH_OK) {
        OverlayLog("overlay", std::string("ARKHAN_MINHOOK_CREATE_FAILED target=") + name +
                              " status=" + MhStatusName(create));
        return false;
    }
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        OverlayLog("overlay", std::string("ARKHAN_MINHOOK_ENABLE_FAILED target=") + name +
                              " status=" + MhStatusName(enable));
        MH_RemoveHook(target);
        *original = nullptr;
        return false;
    }
    return true;
}

bool NativeOverlay::InstallSwapChainHooks() {
    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus == MH_OK) {
        minHookInitialized_ = true;
    } else if (initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        OverlayLog("overlay", std::string("ARKHAN_MINHOOK_INIT_FAILED status=") + MhStatusName(initStatus));
        return false;
    }

    const wchar_t* className = L"ArkhanGraphicsProbeWindow_r23_dx12";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DummyWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (minHookInitialized_) { MH_Uninitialize(); minHookInitialized_ = false; }
        return false;
    }

    HWND window = CreateWindowExW(0, className, L"Arkhan graphics probe", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) {
        if (atom) UnregisterClassW(className, wc.hInstance);
        if (minHookInitialized_) { MH_Uninitialize(); minHookInitialized_ = false; }
        return false;
    }

    IDXGISwapChain* d3d11SwapChain = nullptr;
    ID3D11Device* d3d11ProbeDevice = nullptr;
    ID3D11DeviceContext* d3d11ProbeContext = nullptr;
    DXGI_SWAP_CHAIN_DESC d3d11Desc{};
    d3d11Desc.BufferCount = 1;
    d3d11Desc.BufferDesc.Width = 100;
    d3d11Desc.BufferDesc.Height = 100;
    d3d11Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d3d11Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d3d11Desc.OutputWindow = window;
    d3d11Desc.SampleDesc.Count = 1;
    d3d11Desc.Windowed = TRUE;
    d3d11Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT d11Result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, ARRAYSIZE(requested),
        D3D11_SDK_VERSION, &d3d11Desc, &d3d11SwapChain, &d3d11ProbeDevice, &featureLevel, &d3d11ProbeContext);
    if (FAILED(d11Result)) {
        d11Result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, requested, ARRAYSIZE(requested),
            D3D11_SDK_VERSION, &d3d11Desc, &d3d11SwapChain, &d3d11ProbeDevice, &featureLevel, &d3d11ProbeContext);
    }
    if (SUCCEEDED(d11Result) && d3d11SwapChain) {
        auto** vtable = *reinterpret_cast<void***>(d3d11SwapChain);
        presentTargetDx11_ = vtable[8];
        resizeTargetDx11_ = vtable[13];
    } else {
        OverlayLog("overlay", "ARKHAN_DX11_PROBE_FAILED");
    }
    ReleaseCom(d3d11ProbeContext);
    ReleaseCom(d3d11ProbeDevice);
    ReleaseCom(d3d11SwapChain);

    IDXGIFactory4* factory = nullptr;
    IDXGIAdapter* warpAdapter = nullptr;
    ID3D12Device* d3d12ProbeDevice = nullptr;
    ID3D12CommandQueue* d3d12ProbeQueue = nullptr;
    IDXGISwapChain1* d3d12SwapChain1 = nullptr;
    IDXGISwapChain3* d3d12SwapChain3 = nullptr;
    IDXGISwapChain* d3d12SwapChain = nullptr;

    CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    HRESULT d12Result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                          IID_PPV_ARGS(&d3d12ProbeDevice));
    if (FAILED(d12Result) && factory &&
        SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) && warpAdapter) {
        d12Result = D3D12CreateDevice(warpAdapter, D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&d3d12ProbeDevice));
    }

    if (SUCCEEDED(d12Result) && d3d12ProbeDevice) {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (SUCCEEDED(d3d12ProbeDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d3d12ProbeQueue))) &&
            d3d12ProbeQueue) {
            auto** queueVtable = *reinterpret_cast<void***>(d3d12ProbeQueue);
            executeCommandListsTarget_ = queueVtable[10];

            if (factory) {
                DXGI_SWAP_CHAIN_DESC1 d3d12Desc{};
                d3d12Desc.Width = 100;
                d3d12Desc.Height = 100;
                d3d12Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                d3d12Desc.SampleDesc.Count = 1;
                d3d12Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                d3d12Desc.BufferCount = 2;
                d3d12Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                d3d12Desc.Scaling = DXGI_SCALING_STRETCH;
                d3d12Desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
                if (SUCCEEDED(factory->CreateSwapChainForHwnd(d3d12ProbeQueue, window, &d3d12Desc,
                                                               nullptr, nullptr, &d3d12SwapChain1)) &&
                    d3d12SwapChain1 &&
                    SUCCEEDED(d3d12SwapChain1->QueryInterface(IID_PPV_ARGS(&d3d12SwapChain))) &&
                    d3d12SwapChain) {
                    auto** vtable = *reinterpret_cast<void***>(d3d12SwapChain);
                    presentTargetDx12_ = vtable[8];
                    resizeTargetDx12_ = vtable[13];

                    auto** vtable1 = *reinterpret_cast<void***>(d3d12SwapChain1);
                    present1TargetDx12_ = vtable1[22];

                    if (SUCCEEDED(d3d12SwapChain1->QueryInterface(IID_PPV_ARGS(&d3d12SwapChain3))) &&
                        d3d12SwapChain3) {
                        auto** vtable3 = *reinterpret_cast<void***>(d3d12SwapChain3);
                        resize1TargetDx12_ = vtable3[39];
                    }
                }
            }
        }
    } else {
        OverlayLog("overlay", "ARKHAN_DX12_PROBE_FAILED");
    }

    ReleaseCom(d3d12SwapChain3);
    ReleaseCom(d3d12SwapChain);
    ReleaseCom(d3d12SwapChain1);
    ReleaseCom(d3d12ProbeQueue);
    ReleaseCom(d3d12ProbeDevice);
    ReleaseCom(warpAdapter);
    ReleaseCom(factory);
    DestroyWindow(window);
    if (atom) UnregisterClassW(className, wc.hInstance);

    bool dx11HooksReady = false;
    if (presentTargetDx11_ && resizeTargetDx11_) {
        const bool presentOk = InstallHook(presentTargetDx11_, reinterpret_cast<void*>(&PresentHookDx11),
                                           reinterpret_cast<void**>(&originalPresentDx11_), "Present/DX11");
        const bool resizeOk = InstallHook(resizeTargetDx11_, reinterpret_cast<void*>(&ResizeBuffersHookDx11),
                                          reinterpret_cast<void**>(&originalResizeDx11_), "ResizeBuffers/DX11");
        dx11HooksReady = presentOk && resizeOk;
        if (!dx11HooksReady) {
            if (presentTargetDx11_) { MH_DisableHook(presentTargetDx11_); MH_RemoveHook(presentTargetDx11_); }
            if (resizeTargetDx11_) { MH_DisableHook(resizeTargetDx11_); MH_RemoveHook(resizeTargetDx11_); }
            presentTargetDx11_ = nullptr;
            resizeTargetDx11_ = nullptr;
            originalPresentDx11_ = nullptr;
            originalResizeDx11_ = nullptr;
        }
    }

    bool dx12HooksReady = false;
    {
        const bool sharedPresent = dx11HooksReady && presentTargetDx12_ && presentTargetDx12_ == presentTargetDx11_;
        const bool sharedResize = dx11HooksReady && resizeTargetDx12_ && resizeTargetDx12_ == resizeTargetDx11_;

        bool presentOk = false;
        bool present1Ok = false;
        bool resizeOk = false;
        bool resize1Ok = false;

        if (presentTargetDx12_) {
            if (sharedPresent) {
                originalPresentDx12_ = originalPresentDx11_;
                presentOk = true;
            } else {
                presentOk = InstallHook(presentTargetDx12_, reinterpret_cast<void*>(&PresentHookDx12),
                                        reinterpret_cast<void**>(&originalPresentDx12_), "Present/DX12");
            }
        }
        if (present1TargetDx12_) {
            present1Ok = InstallHook(present1TargetDx12_, reinterpret_cast<void*>(&Present1HookDx12),
                                     reinterpret_cast<void**>(&originalPresent1Dx12_), "Present1/DX12");
        }

        if (resizeTargetDx12_) {
            if (sharedResize) {
                originalResizeDx12_ = originalResizeDx11_;
                resizeOk = true;
            } else {
                resizeOk = InstallHook(resizeTargetDx12_, reinterpret_cast<void*>(&ResizeBuffersHookDx12),
                                       reinterpret_cast<void**>(&originalResizeDx12_), "ResizeBuffers/DX12");
            }
        }
        if (resize1TargetDx12_) {
            resize1Ok = InstallHook(resize1TargetDx12_, reinterpret_cast<void*>(&ResizeBuffers1HookDx12),
                                    reinterpret_cast<void**>(&originalResize1Dx12_), "ResizeBuffers1/DX12");
        }

        dx12HooksReady = presentOk || present1Ok;
        OverlayLog("overlay", std::string("ARKHAN_DX12_PRESENT_PATHS present=") +
                              (presentOk ? "1" : "0") + " present1=" + (present1Ok ? "1" : "0") +
                              " resize=" + (resizeOk ? "1" : "0") + " resize1=" + (resize1Ok ? "1" : "0"));
    }

    bool dx12QueueHookReady = false;
    if (executeCommandListsTarget_) {
        dx12QueueHookReady = InstallHook(executeCommandListsTarget_,
                                         reinterpret_cast<void*>(&ExecuteCommandListsHook),
                                         reinterpret_cast<void**>(&originalExecuteCommandLists_),
                                         "ExecuteCommandLists/DX12");
    }

    if (!dx11HooksReady && !dx12HooksReady) {
        RemoveSwapChainHooks();
        return false;
    }

    if (dx12HooksReady && !dx12QueueHookReady) {
        OverlayLog("overlay", "ARKHAN_DX12_QUEUE_HOOK_UNAVAILABLE renderer=DX12");
    } else if (dx12QueueHookReady) {
        OverlayLog("overlay", "ARKHAN_DX12_QUEUE_HOOK_READY type=DIRECT");
    }
    OverlayLog("overlay", std::string("ARKHAN_GRAPHICS_HOOKS_READY dx11=") +
                          (dx11HooksReady ? "1" : "0") + " dx12=" + (dx12HooksReady ? "1" : "0"));
    return true;
}

void NativeOverlay::RemoveSwapChainHooks() {
    if (present1TargetDx12_) {
        MH_DisableHook(present1TargetDx12_);
        MH_RemoveHook(present1TargetDx12_);
    }
    if (resize1TargetDx12_) {
        MH_DisableHook(resize1TargetDx12_);
        MH_RemoveHook(resize1TargetDx12_);
    }
    if (presentTargetDx12_ && presentTargetDx12_ != presentTargetDx11_) {
        MH_DisableHook(presentTargetDx12_);
        MH_RemoveHook(presentTargetDx12_);
    }
    if (resizeTargetDx12_ && resizeTargetDx12_ != resizeTargetDx11_) {
        MH_DisableHook(resizeTargetDx12_);
        MH_RemoveHook(resizeTargetDx12_);
    }
    if (presentTargetDx11_) {
        MH_DisableHook(presentTargetDx11_);
        MH_RemoveHook(presentTargetDx11_);
    }
    if (resizeTargetDx11_) {
        MH_DisableHook(resizeTargetDx11_);
        MH_RemoveHook(resizeTargetDx11_);
    }
    if (executeCommandListsTarget_) {
        MH_DisableHook(executeCommandListsTarget_);
        MH_RemoveHook(executeCommandListsTarget_);
    }

    presentTargetDx11_ = nullptr;
    resizeTargetDx11_ = nullptr;
    originalPresentDx11_ = nullptr;
    originalResizeDx11_ = nullptr;
    presentTargetDx12_ = nullptr;
    present1TargetDx12_ = nullptr;
    resizeTargetDx12_ = nullptr;
    resize1TargetDx12_ = nullptr;
    originalPresentDx12_ = nullptr;
    originalPresent1Dx12_ = nullptr;
    originalResizeDx12_ = nullptr;
    originalResize1Dx12_ = nullptr;
    executeCommandListsTarget_ = nullptr;
    originalExecuteCommandLists_ = nullptr;
    ReleaseCapturedDx12Queue();

    if (minHookInitialized_) {
        MH_Uninitialize();
        minHookInitialized_ = false;
    }
}

OverlayRendererBackend NativeOverlay::DetectBackend(IDXGISwapChain* swapChain) {
    if (!swapChain) return OverlayRendererBackend::None;

    ID3D12Device* d3d12 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d3d12))) && d3d12) {
        d3d12->Release();
        return OverlayRendererBackend::D3D12;
    }

    ID3D11Device* d3d11 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d3d11))) && d3d11) {
        d3d11->Release();
        return OverlayRendererBackend::D3D11;
    }
    return OverlayRendererBackend::None;
}

HRESULT __stdcall NativeOverlay::PresentHookDx11(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    auto& self = Instance();
    const auto backend = DetectBackend(swapChain);
    if (!self.stopping_.load()) self.OnPresent(swapChain, backend);
    const auto original = self.originalPresentDx11_;
    return original ? original(swapChain, syncInterval, flags) : S_OK;
}

HRESULT __stdcall NativeOverlay::PresentHookDx12(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    auto& self = Instance();
    const auto backend = DetectBackend(swapChain);
    if (!self.stopping_.load()) self.OnPresent(swapChain, backend);
    const auto original = self.originalPresentDx12_;
    return original ? original(swapChain, syncInterval, flags) : S_OK;
}

HRESULT __stdcall NativeOverlay::Present1HookDx12(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                                   const DXGI_PRESENT_PARAMETERS* parameters) {
    auto& self = Instance();
    IDXGISwapChain* baseSwapChain = nullptr;
    if (swapChain) swapChain->QueryInterface(IID_PPV_ARGS(&baseSwapChain));
    if (!self.stopping_.load() && baseSwapChain) {
        const auto backend = DetectBackend(baseSwapChain);
        self.OnPresent(baseSwapChain, backend);
    }
    ReleaseCom(baseSwapChain);
    const auto original = self.originalPresent1Dx12_;
    return original ? original(swapChain, syncInterval, flags, parameters) : S_OK;
}

HRESULT __stdcall NativeOverlay::ResizeBuffersHookDx11(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                                        UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
    auto& self = Instance();
    const auto backend = DetectBackend(swapChain);
    self.OnBeforeResize(swapChain, backend);
    const auto original = self.originalResizeDx11_;
    const HRESULT result = original ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags) : E_FAIL;
    if (SUCCEEDED(result)) self.OnAfterResize(swapChain, backend);
    return result;
}

HRESULT __stdcall NativeOverlay::ResizeBuffersHookDx12(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                                        UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
    auto& self = Instance();
    const auto backend = DetectBackend(swapChain);
    self.OnBeforeResize(swapChain, backend);
    const auto original = self.originalResizeDx12_;
    const HRESULT result = original ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags) : E_FAIL;
    if (SUCCEEDED(result)) self.OnAfterResize(swapChain, backend);
    return result;
}

HRESULT __stdcall NativeOverlay::ResizeBuffers1HookDx12(IDXGISwapChain3* swapChain, UINT bufferCount,
                                                         UINT width, UINT height, DXGI_FORMAT newFormat,
                                                         UINT swapChainFlags, const UINT* creationNodeMask,
                                                         IUnknown* const* presentQueues) {
    auto& self = Instance();
    IDXGISwapChain* baseSwapChain = nullptr;
    if (swapChain) swapChain->QueryInterface(IID_PPV_ARGS(&baseSwapChain));
    const auto backend = DetectBackend(baseSwapChain);
    if (baseSwapChain) self.OnBeforeResize(baseSwapChain, backend);

    const auto original = self.originalResize1Dx12_;
    const HRESULT result = original
        ? original(swapChain, bufferCount, width, height, newFormat, swapChainFlags, creationNodeMask, presentQueues)
        : E_FAIL;
    if (SUCCEEDED(result) && baseSwapChain) self.OnAfterResize(baseSwapChain, backend);
    ReleaseCom(baseSwapChain);
    return result;
}

void __stdcall NativeOverlay::ExecuteCommandListsHook(ID3D12CommandQueue* queue, UINT numCommandLists,
                                                       ID3D12CommandList* const* commandLists) {
    auto& self = Instance();
    if (!self.stopping_.load()) self.CaptureDx12CommandQueue(queue);
    const auto original = self.originalExecuteCommandLists_;
    if (original) original(queue, numCommandLists, commandLists);
}

void NativeOverlay::ToggleMenu() {
    const bool next = !menuOpen_.load();
    menuOpen_.store(next);
    inputOwned_.store(next);

    if (imguiContextCreated_) {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = next;
    }

    if (next) {
        ReleaseCapture();
        ClipCursor(nullptr);
    }

    OverlayLog("overlay", next ? "ARKHAN_OVERLAY_INPUT_OWNED state=open"
                               : "ARKHAN_OVERLAY_INPUT_OWNED state=closed");
}

bool NativeOverlay::EnsureWindowProcHook() {
    if (!gameWindow_) return false;

    const auto hookProc = reinterpret_cast<WNDPROC>(&WindowProcHook);
    const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow_, GWLP_WNDPROC));
    if (current == hookProc) return true;

    if (current) originalWindowProc_ = current;

    SetLastError(0);
    const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        gameWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hookProc)));
    if (!previous && GetLastError() != 0) {
        OverlayLog("overlay", "ARKHAN_OVERLAY_WNDPROC_REHOOK_FAILED");
        return false;
    }
    if (previous && previous != hookProc) originalWindowProc_ = previous;

    OverlayLog("overlay", "ARKHAN_OVERLAY_WNDPROC_REHOOKED priority=top");
    return true;
}

void NativeOverlay::EnforceInputOwnership() {
    if (!inputOwned_.load() || !gameWindow_) return;

    ReleaseCapture();
    ClipCursor(nullptr);

    if (imguiContextCreated_) {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = true;
    }
}

LRESULT CALLBACK NativeOverlay::WindowProcHook(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE || message == WM_DESTROY ||
        (message == WM_ENDSESSION && wParam != 0)) {
        OverlayExitRequested();
    }
    auto& self = Instance();
    std::lock_guard<std::recursive_mutex> imguiLock(self.imguiMutex_);

    if (self.mobVacuumHotkeyCapture_.load() &&
        (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        const int virtualKey = static_cast<int>(wParam);
        if (virtualKey == VK_ESCAPE) self.mobVacuumHotkeyVk_.store(0);
        else if (virtualKey > 0 && virtualKey < 256) {
            self.mobVacuumHotkeyVk_.store(virtualKey);
        }
        self.mobVacuumHotkeyCapture_.store(false);
        self.mobVacuumHotkeyWasDown_.store(true);
        return TRUE;
    }



    const int vacuumHotkey = self.mobVacuumHotkeyVk_.load();
    if (!self.inputOwned_.load() && vacuumHotkey > 0) {
        if ((message == WM_KEYDOWN || message == WM_KEYUP ||
             message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
            static_cast<int>(wParam) == vacuumHotkey) {
            return TRUE;
        }
        if (message == WM_INPUT && RawInputContainsKeyboardKey(lParam, vacuumHotkey)) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
    if (message == WM_XBUTTONUP && HIWORD(wParam) == XBUTTON1) {
        self.ToggleMenu();
        return TRUE;
    }
    if (message == WM_XBUTTONDOWN && HIWORD(wParam) == XBUTTON1) {
        return TRUE;
    }

    if (self.inputOwned_.load() && self.imguiContextCreated_ && self.imguiWin32Ready_) {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    }

    if (message == WM_INPUT && !self.inputOwned_.load() && RawInputContainsMouse4(lParam)) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (self.inputOwned_.load()) {
        if (message == WM_INPUT) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_INPUT_DEVICE_CHANGE) {
            return 0;
        }
        if (message == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (message == WM_MOUSEACTIVATE) {
            return MA_ACTIVATE;
        }
        if (IsMouseMessage(message) || IsKeyboardMessage(message)) {
            return TRUE;
        }
    }

    return self.originalWindowProc_ ? CallWindowProcW(self.originalWindowProc_, hwnd, message, wParam, lParam)
                                    : DefWindowProcW(hwnd, message, wParam, lParam);
}

bool NativeOverlay::CreateDx11RenderTarget(IDXGISwapChain* swapChain) {
    ReleaseDx11RenderTarget();
    if (!dx11Device_ || !swapChain) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) return false;
    const HRESULT result = dx11Device_->CreateRenderTargetView(backBuffer, nullptr, &dx11RenderTarget_);
    backBuffer->Release();
    return SUCCEEDED(result) && dx11RenderTarget_;
}

void NativeOverlay::ReleaseDx11RenderTarget() {
    ReleaseCom(dx11RenderTarget_);
}

bool NativeOverlay::InitializeCommonImGui(HWND gameWindow) {
    if (!gameWindow) return false;

    gameWindow_ = gameWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated_ = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, nullptr,
                                      io.Fonts->GetGlyphRangesDefault())) {
        io.Fonts->AddFontDefault();
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.97f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.68f, 0.76f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.015f, 0.040f, 0.070f, 0.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.015f, 0.055f, 0.095f, 0.46f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.012f, 0.035f, 0.060f, 0.97f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.23f, 0.72f, 0.88f, 0.56f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.19f, 0.63f, 0.80f, 0.58f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.018f, 0.075f, 0.13f, 0.52f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.025f, 0.12f, 0.19f, 0.68f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.018f, 0.075f, 0.13f, 0.44f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.018f, 0.075f, 0.12f, 0.88f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.035f, 0.16f, 0.23f, 0.96f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.045f, 0.23f, 0.31f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.91f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.70f, 0.88f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.48f, 0.91f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.025f, 0.10f, 0.16f, 0.88f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.045f, 0.22f, 0.30f, 0.96f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.055f, 0.34f, 0.43f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.035f, 0.14f, 0.21f, 0.82f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.055f, 0.27f, 0.36f, 0.96f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.36f, 0.45f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.018f, 0.065f, 0.11f, 0.82f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.045f, 0.24f, 0.33f, 0.96f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.045f, 0.31f, 0.41f, 0.98f);

    if (!ImGui_ImplWin32_Init(gameWindow_)) return false;
    imguiWin32Ready_ = true;

    originalWindowProc_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow_, GWLP_WNDPROC));
    return EnsureWindowProcHook();
}

bool NativeOverlay::InitializeDx11(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&dx11Device_))) || !dx11Device_) return false;

    dx11Device_->GetImmediateContext(&dx11Context_);
    if (!dx11Context_ || !CreateDx11RenderTarget(swapChain)) return false;

    if (!ImGui_ImplDX11_Init(dx11Device_, dx11Context_)) return false;
    imguiDx11Ready_ = true;
    return true;
}

void NativeOverlay::CaptureDx12CommandQueue(ID3D12CommandQueue* queue) {
    if (!queue) return;
    const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
    if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;

    std::lock_guard lock(dx12QueueMutex_);
    if (capturedDx12CommandQueue_ == queue) return;
    if (dx12QueueLocked_.load(std::memory_order_acquire)) return;

    ReleaseCom(capturedDx12CommandQueue_);
    queue->AddRef();
    capturedDx12CommandQueue_ = queue;
}

ID3D12CommandQueue* NativeOverlay::AcquireCapturedDx12Queue(ID3D12Device* device) {
    if (!device) return nullptr;
    std::lock_guard lock(dx12QueueMutex_);
    if (!capturedDx12CommandQueue_) return nullptr;

    ID3D12Device* queueDevice = nullptr;
    if (FAILED(capturedDx12CommandQueue_->GetDevice(IID_PPV_ARGS(&queueDevice))) || !queueDevice) {
        ReleaseCom(capturedDx12CommandQueue_);
        return nullptr;
    }

    IUnknown* expectedIdentity = nullptr;
    IUnknown* queueIdentity = nullptr;
    device->QueryInterface(IID_PPV_ARGS(&expectedIdentity));
    queueDevice->QueryInterface(IID_PPV_ARGS(&queueIdentity));
    const bool sameDevice = expectedIdentity && queueIdentity && expectedIdentity == queueIdentity;
    ReleaseCom(expectedIdentity);
    ReleaseCom(queueIdentity);
    ReleaseCom(queueDevice);

    if (!sameDevice) {
        ReleaseCom(capturedDx12CommandQueue_);
        return nullptr;
    }

    capturedDx12CommandQueue_->AddRef();
    return capturedDx12CommandQueue_;
}

void NativeOverlay::ReleaseCapturedDx12Queue() {
    std::lock_guard lock(dx12QueueMutex_);
    ReleaseCom(capturedDx12CommandQueue_);
}

bool NativeOverlay::CreateDx12Resources(IDXGISwapChain* swapChain) {
    if (!swapChain || !dx12Device_ || !dx12CommandQueue_) return false;

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    if (FAILED(swapChain->GetDesc(&swapDesc)) || swapDesc.BufferCount == 0) return false;

    const UINT frameCount = swapDesc.BufferCount;
    dx12RtvFormat_ = swapDesc.BufferDesc.Format;
    dx12Frames_.assign(frameCount, Dx12FrameContext{});

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = frameCount;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(dx12Device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&dx12RtvHeap_)))) return false;
    dx12RtvDescriptorSize_ = dx12Device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 2;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dx12Device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&dx12SrvHeap_)))) return false;
    dx12SrvDescriptorSize_ = dx12Device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx12RtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < frameCount; ++i) {
        auto& frame = dx12Frames_[i];
        if (FAILED(dx12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&frame.allocator)))) return false;
        if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&frame.backBuffer))) || !frame.backBuffer) return false;
        if (dx12RtvFormat_ == DXGI_FORMAT_UNKNOWN) dx12RtvFormat_ = frame.backBuffer->GetDesc().Format;
        frame.rtvHandle = rtv.ptr;
        dx12Device_->CreateRenderTargetView(frame.backBuffer, nullptr, rtv);
        rtv.ptr += dx12RtvDescriptorSize_;
    }

    if (dx12Frames_.empty() || !dx12Frames_[0].allocator) return false;
    if (FAILED(dx12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               dx12Frames_[0].allocator, nullptr,
                                               IID_PPV_ARGS(&dx12CommandList_))) || !dx12CommandList_) {
        return false;
    }
    if (FAILED(dx12CommandList_->Close())) return false;

    if (FAILED(dx12Device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx12Fence_)))) return false;
    dx12FenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dx12FenceEvent_) return false;
    dx12NextFenceValue_ = 1;
    return true;
}

void NativeOverlay::WaitForDx12Frame(Dx12FrameContext& frame) {
    if (!dx12Fence_ || !dx12FenceEvent_ || frame.fenceValue == 0) return;
    if (dx12Fence_->GetCompletedValue() >= frame.fenceValue) return;
    if (SUCCEEDED(dx12Fence_->SetEventOnCompletion(frame.fenceValue, dx12FenceEvent_))) {
        WaitForSingleObject(dx12FenceEvent_, INFINITE);
    }
}

void NativeOverlay::WaitForDx12Gpu() {
    if (!dx12CommandQueue_ || !dx12Fence_ || !dx12FenceEvent_) return;
    const UINT64 value = dx12NextFenceValue_++;
    if (FAILED(dx12CommandQueue_->Signal(dx12Fence_, value))) return;
    if (dx12Fence_->GetCompletedValue() >= value) return;
    if (SUCCEEDED(dx12Fence_->SetEventOnCompletion(value, dx12FenceEvent_))) {
        WaitForSingleObject(dx12FenceEvent_, INFINITE);
    }
}

void NativeOverlay::ReleaseDx12Resources(bool waitForGpu) {
    if (waitForGpu) WaitForDx12Gpu();

    for (auto& frame : dx12Frames_) {
        ReleaseCom(frame.backBuffer);
        ReleaseCom(frame.allocator);
        frame.rtvHandle = 0;
        frame.fenceValue = 0;
    }
    dx12Frames_.clear();

    ReleaseCom(dx12BackgroundTexture_);
    ReleaseCom(dx12CommandList_);
    ReleaseCom(dx12SrvHeap_);
    ReleaseCom(dx12RtvHeap_);
    ReleaseCom(dx12Fence_);
    if (dx12FenceEvent_) {
        CloseHandle(dx12FenceEvent_);
        dx12FenceEvent_ = nullptr;
    }
    dx12RtvDescriptorSize_ = 0;
    dx12SrvDescriptorSize_ = 0;
    dx12RtvFormat_ = DXGI_FORMAT_UNKNOWN;
    dx12NextFenceValue_ = 1;
}

bool NativeOverlay::InitializeDx12(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&dx12Device_))) || !dx12Device_) return false;

    dx12CommandQueue_ = AcquireCapturedDx12Queue(dx12Device_);
    if (!dx12CommandQueue_) {
        std::lock_guard lock(statusMutex_);
        status_ = "DX12 detected; waiting for DIRECT command queue";
        return false;
    }

    if (!CreateDx12Resources(swapChain)) return false;

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device = dx12Device_;
    initInfo.CommandQueue = dx12CommandQueue_;
    initInfo.NumFramesInFlight = static_cast<int>(dx12Frames_.size());
    initInfo.RTVFormat = dx12RtvFormat_;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = dx12SrvHeap_;
    initInfo.LegacySingleSrvCpuDescriptor = dx12SrvHeap_->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = dx12SrvHeap_->GetGPUDescriptorHandleForHeapStart();
    if (!ImGui_ImplDX12_Init(&initInfo)) return false;

    imguiDx12Ready_ = true;
    dx12QueueLocked_.store(true, std::memory_order_release);
    return true;
}

bool NativeOverlay::InitializeImGui(IDXGISwapChain* swapChain, OverlayRendererBackend backend) {
    std::lock_guard<std::recursive_mutex> imguiLock(imguiMutex_);
    if (!swapChain || backend == OverlayRendererBackend::None) return false;

    const bool backendReady = backend == OverlayRendererBackend::D3D11
        ? (imguiDx11Ready_ && dx11Device_ && dx11Context_ && dx11RenderTarget_)
        : (imguiDx12Ready_ && dx12Device_ && dx12CommandQueue_ && dx12CommandList_ && !dx12Frames_.empty());
    if (activeSwapChain_ == swapChain && rendererBackend_ == backend && imguiContextCreated_ &&
        imguiWin32Ready_ && backendReady) {
        return true;
    }

    ShutdownImGui();

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow) return false;

    activeSwapChain_ = swapChain;
    rendererBackend_ = backend;
    if (!InitializeCommonImGui(desc.OutputWindow)) {
        ShutdownImGui();
        return false;
    }

    const bool rendererReady = backend == OverlayRendererBackend::D3D11
        ? InitializeDx11(swapChain)
        : InitializeDx12(swapChain);
    if (!rendererReady) {
        ShutdownImGui();
        return false;
    }

    if (!InitializeBackgroundTexture()) {
        backgroundTextureId_ = 0;
        OverlayLog("overlay", "ARKHAN_OVERLAY_BACKGROUND_UNAVAILABLE; continuing with plain window background");
    }

    ready_.store(true);
    {
        std::lock_guard lock(statusMutex_);
        status_ = std::string("Dear ImGui + ") + RendererName() + " ready (Mouse4)";
    }
    OverlayLog("overlay", std::string("ARKHAN_OVERLAY_DEVICE_READY backend=DearImGui+") +
                          RendererName() + " hook=MinHook key=Mouse4");
    return true;
}

bool NativeOverlay::InitializeBackgroundTexture() {
    HMODULE module = GetModuleHandleW(L"ArkhanOverlay.dll");
    if (!module) return false;

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(kArkhanBackgroundResourceId), RT_RCDATA);
    if (!resource) return false;
    const DWORD resourceSize = SizeofResource(module, resource);
    HGLOBAL loadedResource = LoadResource(module, resource);
    const auto* resourceBytes = loadedResource
        ? static_cast<const std::uint8_t*>(LockResource(loadedResource))
        : nullptr;
    if (!resourceBytes || resourceSize == 0) return false;

    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(coInit);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) return false;

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    bool decoded = false;

    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) break;
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(resourceBytes), resourceSize))) break;
        if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 || width > 8192 || height > 8192) break;
        if (static_cast<std::uint64_t>(width) * height * 4u > std::numeric_limits<UINT>::max()) break;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                         nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;

        const UINT stride = width * 4u;
        const UINT bufferSize = stride * height;
        pixels.resize(bufferSize);
        if (FAILED(converter->CopyPixels(nullptr, stride, bufferSize, pixels.data()))) {
            pixels.clear();
            break;
        }
        decoded = true;
    } while (false);

    ReleaseCom(converter);
    ReleaseCom(frame);
    ReleaseCom(decoder);
    ReleaseCom(stream);
    ReleaseCom(factory);
    if (shouldUninitialize) CoUninitialize();
    if (!decoded) return false;

    return rendererBackend_ == OverlayRendererBackend::D3D11
        ? CreateDx11BackgroundTexture(pixels.data(), width, height)
        : rendererBackend_ == OverlayRendererBackend::D3D12
            ? CreateDx12BackgroundTexture(pixels.data(), width, height)
            : false;
}

bool NativeOverlay::CreateDx11BackgroundTexture(const std::uint8_t* pixels, std::uint32_t width,
                                                 std::uint32_t height) {
    if (!dx11Device_ || !pixels || width == 0 || height == 0) return false;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels;
    initialData.SysMemPitch = width * 4u;

    if (FAILED(dx11Device_->CreateTexture2D(&textureDesc, &initialData, &dx11BackgroundTexture_))) return false;
    if (FAILED(dx11Device_->CreateShaderResourceView(dx11BackgroundTexture_, nullptr, &dx11BackgroundView_))) return false;

    backgroundTextureId_ = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(dx11BackgroundView_));
    return backgroundTextureId_ != 0;
}

bool NativeOverlay::CreateDx12BackgroundTexture(const std::uint8_t* pixels, std::uint32_t width,
                                                 std::uint32_t height) {
    if (!dx12Device_ || !dx12CommandQueue_ || !dx12CommandList_ || !dx12SrvHeap_ ||
        dx12SrvDescriptorSize_ == 0 || dx12Frames_.empty() || !pixels || width == 0 || height == 0) return false;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;
    if (FAILED(dx12Device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&dx12BackgroundTexture_)))) return false;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowBytes = 0;
    UINT64 uploadBytes = 0;
    dx12Device_->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rowCount, &rowBytes, &uploadBytes);
    if (uploadBytes == 0 || uploadBytes > std::numeric_limits<SIZE_T>::max() || rowCount != height || rowBytes < width * 4u) return false;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadBytes;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* uploadBuffer = nullptr;
    if (FAILED(dx12Device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&uploadBuffer)))) return false;

    D3D12_RANGE readRange{0, 0};
    std::uint8_t* mapped = nullptr;
    HRESULT result = uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (FAILED(result) || !mapped) {
        ReleaseCom(uploadBuffer);
        return false;
    }
    for (UINT row = 0; row < height; ++row) {
        std::memcpy(mapped + footprint.Offset + static_cast<SIZE_T>(row) * footprint.Footprint.RowPitch,
                    pixels + static_cast<SIZE_T>(row) * width * 4u, static_cast<SIZE_T>(width) * 4u);
    }
    D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(uploadBytes)};
    uploadBuffer->Unmap(0, &writtenRange);

    auto& frame = dx12Frames_.front();
    WaitForDx12Frame(frame);
    result = frame.allocator ? frame.allocator->Reset() : E_FAIL;
    if (SUCCEEDED(result)) result = dx12CommandList_->Reset(frame.allocator, nullptr);
    if (FAILED(result)) {
        ReleaseCom(uploadBuffer);
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = dx12BackgroundTexture_;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = uploadBuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    dx12CommandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    D3D12_RESOURCE_BARRIER toShaderResource{};
    toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource.Transition.pResource = dx12BackgroundTexture_;
    toShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    dx12CommandList_->ResourceBarrier(1, &toShaderResource);
    result = dx12CommandList_->Close();
    if (FAILED(result)) {
        ReleaseCom(uploadBuffer);
        return false;
    }

    ID3D12CommandList* commandLists[] = {dx12CommandList_};
    dx12CommandQueue_->ExecuteCommandLists(1, commandLists);
    const UINT64 fenceValue = dx12NextFenceValue_++;
    result = dx12CommandQueue_->Signal(dx12Fence_, fenceValue);
    if (FAILED(result)) {
        WaitForDx12Gpu();
        ReleaseCom(uploadBuffer);
        return false;
    }
    frame.fenceValue = fenceValue;
    WaitForDx12Frame(frame);
    ReleaseCom(uploadBuffer);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = dx12SrvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += dx12SrvDescriptorSize_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dx12SrvHeap_->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += dx12SrvDescriptorSize_;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    dx12Device_->CreateShaderResourceView(dx12BackgroundTexture_, &srvDesc, cpuHandle);
    backgroundTextureId_ = gpuHandle.ptr;
    return backgroundTextureId_ != 0;
}

void NativeOverlay::ShutdownImGui() {
    std::lock_guard<std::recursive_mutex> imguiLock(imguiMutex_);
    ready_.store(false);
    dx12QueueLocked_.store(false, std::memory_order_release);

    if (rendererBackend_ == OverlayRendererBackend::D3D12) {
        WaitForDx12Gpu();
    }

    if (imguiDx12Ready_) {
        ImGui_ImplDX12_Shutdown();
        imguiDx12Ready_ = false;
    }
    if (imguiDx11Ready_) {
        ImGui_ImplDX11_Shutdown();
        imguiDx11Ready_ = false;
    }
    if (imguiWin32Ready_) {
        ImGui_ImplWin32_Shutdown();
        imguiWin32Ready_ = false;
    }

    if (gameWindow_ && originalWindowProc_) {
        SetWindowLongPtrW(gameWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProc_));
    }
    originalWindowProc_ = nullptr;
    gameWindow_ = nullptr;
    inputOwned_.store(false);

    if (imguiContextCreated_) {
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }

    ReleaseDx11RenderTarget();
    ReleaseCom(dx11BackgroundView_);
    ReleaseCom(dx11BackgroundTexture_);
    backgroundTextureId_ = 0;
    ReleaseCom(dx11Context_);
    ReleaseCom(dx11Device_);

    ReleaseDx12Resources(false);
    ReleaseCom(dx12CommandQueue_);
    ReleaseCom(dx12Device_);

    activeSwapChain_ = nullptr;
    rendererBackend_ = OverlayRendererBackend::None;
}

bool NativeOverlay::RenderDx12DrawData(IDXGISwapChain* swapChain) {
    if (!swapChain || !dx12CommandQueue_ || !dx12CommandList_ || !dx12Fence_ || !dx12SrvHeap_) return false;

    IDXGISwapChain3* swapChain3 = nullptr;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3) return false;

    const UINT frameIndex = swapChain3->GetCurrentBackBufferIndex();
    if (frameIndex >= dx12Frames_.size()) {
        swapChain3->Release();
        return false;
    }

    auto& frame = dx12Frames_[frameIndex];
    WaitForDx12Frame(frame);
    if (!frame.allocator || !frame.backBuffer || frame.rtvHandle == 0) {
        swapChain3->Release();
        return false;
    }

    if (FAILED(frame.allocator->Reset()) || FAILED(dx12CommandList_->Reset(frame.allocator, nullptr))) {
        swapChain3->Release();
        return false;
    }

    D3D12_RESOURCE_BARRIER toRender{};
    toRender.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRender.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRender.Transition.pResource = frame.backBuffer;
    toRender.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRender.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRender.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    dx12CommandList_->ResourceBarrier(1, &toRender);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    rtv.ptr = frame.rtvHandle;
    dx12CommandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {dx12SrvHeap_};
    dx12CommandList_->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12CommandList_);

    D3D12_RESOURCE_BARRIER toPresent = toRender;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    dx12CommandList_->ResourceBarrier(1, &toPresent);

    if (FAILED(dx12CommandList_->Close())) {
        swapChain3->Release();
        return false;
    }

    ID3D12CommandList* lists[] = {dx12CommandList_};
    dx12CommandQueue_->ExecuteCommandLists(1, lists);
    const UINT64 fenceValue = dx12NextFenceValue_++;
    if (SUCCEEDED(dx12CommandQueue_->Signal(dx12Fence_, fenceValue))) {
        frame.fenceValue = fenceValue;
    }

    swapChain3->Release();
    return true;
}

const char* NativeOverlay::RendererName() const {
    switch (rendererBackend_) {
        case OverlayRendererBackend::D3D11: return "D3D11";
        case OverlayRendererBackend::D3D12: return "D3D12";
        default: return "uninitialized";
    }
}


void NativeOverlay::PollMobVacuumHotkey() {
    const int virtualKey = mobVacuumHotkeyVk_.load();
    if (virtualKey <= 0 || mobVacuumHotkeyCapture_.load()) {
        mobVacuumHotkeyWasDown_.store(false);
        return;
    }

    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool wasDown = mobVacuumHotkeyWasDown_.exchange(down);
    if (!down || wasDown || menuOpen_.load()) return;

    std::lock_guard lock(controlsMutex_);
    controls_.mobVacuumEnabled = !controls_.mobVacuumEnabled;
    if (controls_.mobVacuumEnabled) controls_.mobVacuumStun = false;
    ++controls_.revision;
}


void NativeOverlay::OnPresent(IDXGISwapChain* swapChain, OverlayRendererBackend backend) {
    std::lock_guard<std::recursive_mutex> imguiLock(imguiMutex_);
    if (!InitializeImGui(swapChain, backend)) return;

    EnsureWindowProcHook();
    EnforceInputOwnership();
    PollMobVacuumHotkey();

    if (rendererBackend_ == OverlayRendererBackend::D3D11) ImGui_ImplDX11_NewFrame();
    else if (rendererBackend_ == OverlayRendererBackend::D3D12) ImGui_ImplDX12_NewFrame();
    else return;

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = menuOpen_.load();

    RenderEsp();
    if (menuOpen_.load()) RenderMenu();

    ImGui::Render();

    if (rendererBackend_ == OverlayRendererBackend::D3D11) {
        ID3D11RenderTargetView* previousRtv = nullptr;
        ID3D11DepthStencilView* previousDsv = nullptr;
        dx11Context_->OMGetRenderTargets(1, &previousRtv, &previousDsv);
        dx11Context_->OMSetRenderTargets(1, &dx11RenderTarget_, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        dx11Context_->OMSetRenderTargets(1, &previousRtv, previousDsv);
        ReleaseCom(previousRtv);
        ReleaseCom(previousDsv);
    } else {
        RenderDx12DrawData(swapChain);
    }
}

void NativeOverlay::OnBeforeResize(IDXGISwapChain* swapChain, OverlayRendererBackend backend) {
    std::lock_guard<std::recursive_mutex> imguiLock(imguiMutex_);
    if (swapChain != activeSwapChain_ || backend != rendererBackend_) return;
    ShutdownImGui();
}

void NativeOverlay::OnAfterResize(IDXGISwapChain*, OverlayRendererBackend) {
}

void NativeOverlay::DisableAll() {
    std::lock_guard lock(controlsMutex_);
    controls_.infiniteStamina = false;
    controls_.noFallDamage = false;
    controls_.speedBoost = false;
    controls_.autoCollect = false;
    controls_.autoChest = false;
    controls_.playerGodMode = false;
    controls_.playerOverloadRelief = false;
    controls_.captureAssistant = false;
    controls_.captureAutoThrow = false;
    controls_.autoDialogueSkip = false;
    controls_.autoCutsceneSkip = false;
    controls_.fovOverride = false;
    controls_.interconnectionGodMode = false;
    controls_.interconnectionNoCooldown = false;
    controls_.interconnectionNoCost = false;
    controls_.interconnectionDamage = false;
    controls_.interconnectionAttackSpeed = false;
    controls_.interconnectionAttackFurious = false;
    controls_.interconnectionAttackEnhance = false;
    controls_.interconnectionAttackHunt = false;
    controls_.interconnectionCaptureBoost = false;
    controls_.interconnectionInfiUltimate = false;
    controls_.espEnabled = false;
    controls_.eggHasteEnabled = false;
    controls_.eggHasteEggMovement = false;
    controls_.eggHasteDashBoost = false;
    controls_.eggHasteNoDashCooldown = false;
    controls_.eggHasteFastCarryBoost = false;
    controls_.eggHasteEspEnabled = false;
    controls_.eggHasteRemoveViewFog = false;
    controls_.eggHasteRevealFullMap = false;
    controls_.autofarmEnabled = false;
    controls_.autoquestEnabled = false;
    controls_.mobVacuumEnabled = false;
    controls_.mobVacuumStun = false;
    ++controls_.revision;
}

void NativeOverlay::RenderMenu() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workSize = viewport ? viewport->WorkSize : ImVec2(1280.0f, 720.0f);
    const float maxWidth = std::max(320.0f, std::min(workSize.x * 0.92f, 1500.0f));
    const float maxHeight = std::max(260.0f, std::min(workSize.y * 0.92f, 950.0f));
    const float initialWidth = std::min(maxWidth, maxHeight * kArkhanBackgroundAspect);
    const float initialHeight = initialWidth / kArkhanBackgroundAspect;
    const ImVec2 minSize(std::min(620.0f, maxWidth), std::min(420.0f, maxHeight));
    const ImVec2 maxSize(std::max(minSize.x, workSize.x * 0.98f),
                         std::max(minSize.y, workSize.y * 0.98f));
    ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
    ImGui::SetNextWindowSize(ImVec2(initialWidth, initialHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(workPos.x + (workSize.x - initialWidth) * 0.5f,
                                  workPos.y + (workSize.y - initialHeight) * 0.5f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(backgroundTextureId_ != 0 ? 0.0f : 0.96f);

    bool open = menuOpen_.load();
    if (!ImGui::Begin("Arkhan", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            menuOpen_.store(false);
            inputOwned_.store(false);
            ImGui::GetIO().MouseDrawCursor = false;
        }
        return;
    }

    if (backgroundTextureId_ != 0) {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMax(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
        ImVec2 uvMin(0.0f, 0.0f);
        ImVec2 uvMax(1.0f, 1.0f);
        const float windowAspect = windowSize.x / std::max(1.0f, windowSize.y);
        if (windowAspect > kArkhanBackgroundAspect) {
            const float visibleHeight = kArkhanBackgroundAspect / windowAspect;
            uvMin.y = (1.0f - visibleHeight) * 0.5f;
            uvMax.y = uvMin.y + visibleHeight;
        } else {
            const float visibleWidth = windowAspect / kArkhanBackgroundAspect;
            uvMin.x = (1.0f - visibleWidth) * 0.5f;
            uvMax.x = uvMin.x + visibleWidth;
        }

        ImDrawList* backgroundDrawList = ImGui::GetBackgroundDrawList();
        backgroundDrawList->AddImageRounded(static_cast<ImTextureID>(backgroundTextureId_), windowPos, windowMax,
                                            uvMin, uvMax, IM_COL32_WHITE, ImGui::GetStyle().WindowRounding,
                                            ImDrawFlags_RoundCornersAll);
        backgroundDrawList->AddRectFilled(windowPos, windowMax, IM_COL32(3, 13, 25, 154),
                                          ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersAll);
    }

    if (!open) {
        menuOpen_.store(false);
        inputOwned_.store(false);
        ImGui::GetIO().MouseDrawCursor = false;
    }

    NativeUiControls local = ReadControls();
    bool changed = false;
    bool presentationChanged = false;
    bool eggAuxChanged = false;
    bool eggQualityChanged = false;
    bool autofarmChanged = false;
    bool autoquestChanged = false;
    bool profileApplied = false;
    const auto section = [](const char* name) {
        ImGui::Spacing();
        ImGui::TextUnformatted(name);
        ImGui::Separator();
    };

    if (ImGui::BeginTabBar("arkhan_root_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("PROFILES")) {
            static std::array<char, 64> profileName{};
            const auto profiles = ListProfiles();
            if (selectedProfile_.empty() && !profiles.empty()) selectedProfile_ = profiles.front();
            const char* preview = selectedProfile_.empty() ? "Select profile" : selectedProfile_.c_str();
            if (ImGui::BeginCombo("Saved profiles", preview)) {
                for (const auto& profile : profiles) {
                    const bool selected = profile == selectedProfile_;
                    if (ImGui::Selectable(profile.c_str(), selected)) selectedProfile_ = profile;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::InputText("Profile name", profileName.data(), profileName.size());
            if (ImGui::Button("Save current settings")) {
                const std::string name = profileName[0] ? profileName.data() : selectedProfile_;
                if (SaveProfileFile(name, local, mobVacuumHotkeyVk_.load())) {
                    selectedProfile_ = SafeProfileName(name);
                    profileStatus_ = "Saved: " + selectedProfile_;
                } else {
                    profileStatus_ = "Save failed";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply profile")) {
                NativeUiControls loaded{};
                int vacuumKey = mobVacuumHotkeyVk_.load();
                if (LoadProfileFile(selectedProfile_, loaded, vacuumKey)) {
                    loaded.revision = local.revision;
                    loaded.autofarmResetRequest = local.autofarmResetRequest;
                    loaded.autoquestTeleportRequest = local.autoquestTeleportRequest;
                    loaded.autoquestSelectedTemplateId = 0;
                    loaded.autoquestSelectedMarkId = 0;
                    loaded.autoquestSelectedMarkRealSceneId = 0;
                    local = std::move(loaded);
                    mobVacuumHotkeyVk_.store(vacuumKey);
                    mobVacuumHotkeyWasDown_.store(false);
                    profileApplied = true;
                    profileStatus_ = "Applied: " + selectedProfile_;
                } else {
                    profileStatus_ = "Select a saved profile";
                }
            }
            if (!profileStatus_.empty()) ImGui::TextUnformatted(profileStatus_.c_str());
            ImGui::TextDisabled("Profiles save toggles, filters, sliders and hotkeys.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("PLAYER")) {
            section("Movement");
            changed |= ImGui::Checkbox("Infinite Stamina", &local.infiniteStamina);
            changed |= ImGui::Checkbox("No Fall Damage", &local.noFallDamage);
            changed |= ImGui::Checkbox("Speed Boost", &local.speedBoost);
            changed |= ImGui::SliderFloat("Speed", &local.speedMultiplier, 1.0f, 3.0f, "%.2fx",
                                          ImGuiSliderFlags_AlwaysClamp);

            section("Camera");
            changed |= ImGui::Checkbox("FOV Override", &local.fovOverride);
            changed |= ImGui::SliderInt("Field of view", &local.fov, 45, 120, "%d",
                                        ImGuiSliderFlags_AlwaysClamp);
            section("Protection");
            changed |= ImGui::Checkbox("God Mode", &local.playerGodMode);
            changed |= ImGui::Checkbox("Remove Overload", &local.playerOverloadRelief);

            section("UID appearance (visual only)");
            presentationChanged |= ImGui::Checkbox("Replace my displayed UID", &local.hudUidOverrideEnabled);
            std::array<char, 96> uidDisplayText{};
            std::snprintf(uidDisplayText.data(), uidDisplayText.size(), "%s", local.hudUidOverrideText.c_str());
            if (ImGui::InputText("Displayed text", uidDisplayText.data(), uidDisplayText.size())) {
                local.hudUidOverrideText = uidDisplayText.data();
                presentationChanged = true;
            }
            ImGui::TextDisabled("Changes only your visible UID labels; account/network UID is unchanged.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("INTERACTIONS")) {
            section("Collection");
            changed |= ImGui::Checkbox("Auto Collect", &local.autoCollect);
            changed |= ImGui::SliderInt("Collect range", &local.collectRange, 3, 250, "%dm",
                                        ImGuiSliderFlags_AlwaysClamp);

            section("Chests");
            changed |= ImGui::Checkbox("Auto Chest", &local.autoChest);
            changed |= ImGui::SliderInt("Chest range", &local.chestRange, 3, 250, "%dm",
                                        ImGuiSliderFlags_AlwaysClamp);

            section("Skip");
            changed |= ImGui::Checkbox("Auto Dialogue Skip", &local.autoDialogueSkip);
            changed |= ImGui::Checkbox("Auto Cutscene Skip", &local.autoCutsceneSkip);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("CAPTURE")) {
            changed |= ImGui::Checkbox("Capture Assistant", &local.captureAssistant);
            ImGui::BeginDisabled(!local.captureAssistant);
            static const char* captureModes[] = { "Nearest", "ESP filtered", "Species", "Marked / game lock" };
            changed |= ImGui::Combo("Target mode", &local.captureTargetMode, captureModes, IM_ARRAYSIZE(captureModes));
            changed |= ImGui::SliderInt("Range", &local.captureRange, 5, 120, "%dm",
                                        ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderInt("Minimum chance", &local.captureMinPercent, 0, 100, "%d%%",
                                        ImGuiSliderFlags_AlwaysClamp);
            if (local.captureTargetMode == 2) {
                std::array<char, 96> species{};
                std::snprintf(species.data(), species.size(), "%s", local.captureSpecies.c_str());
                if (ImGui::InputText("Target species", species.data(), species.size())) {
                    local.captureSpecies = species.data();
                    changed = true;
                }
            }
            changed |= ImGui::Checkbox("Auto throw", &local.captureAutoThrow);
            ImGui::BeginDisabled(!local.captureAutoThrow);
            changed |= ImGui::Checkbox("Fast throw", &local.captureFastThrow);
            changed |= ImGui::Checkbox("Skip camera transition", &local.captureSkipCameraTransition);
            changed |= ImGui::SliderFloat("Throw animation speed", &local.captureThrowAnimSpeed, 1.0f, 4.0f, "%.2fx",
                                          ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Aniicapsula speed", &local.captureBallSpeed, 1.0f, 4.0f, "%.2fx",
                                          ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Recovery delay", &local.captureRecoveryDelay, 0.10f, 2.0f, "%.2fs",
                                          ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ESP")) {
            changed |= ImGui::Checkbox("Enable ESP", &local.espEnabled);
            ImGui::BeginDisabled(!local.espEnabled);
            changed |= ImGui::SliderInt("Render distance", &local.espRange, 20, 500, "%dm",
                                        ImGuiSliderFlags_AlwaysClamp);
            section("Entities");
            changed |= ImGui::Checkbox("Collectables", &local.espCollect);
            changed |= ImGui::Checkbox("Chests", &local.espChest);
            changed |= ImGui::Checkbox("Enemies", &local.espEnemy);
            changed |= ImGui::Checkbox("Creatures", &local.espCreature);
            changed |= ImGui::Checkbox("NPCs", &local.espNpc);

            section("Labels");
            ImGui::BeginDisabled(!local.espCreature && !local.espEnemy);
            changed |= ImGui::Checkbox("Normal", &local.espCreatureNormal);
            changed |= ImGui::Checkbox("Cintilante", &local.espCreatureShiny);
            changed |= ImGui::Checkbox("Prismana", &local.espCreatureRainbow);
            changed |= ImGui::Checkbox("Boss", &local.espCreatureBoss);
            changed |= ImGui::Checkbox("Elite", &local.espCreatureElite);
            changed |= ImGui::Checkbox("Variant", &local.espCreatureVariant);
            changed |= ImGui::Checkbox("Magic / Dark", &local.espCreatureMagic);
            ImGui::EndDisabled();

            section("Species");
            static const char* speciesModes[] = { "All species", "Whitelist", "Blacklist" };
            changed |= ImGui::Combo("Filter", &local.espSpeciesFilterMode, speciesModes, IM_ARRAYSIZE(speciesModes));
            if (local.espSpeciesFilterMode != 0) {
                std::array<char, 160> filter{};
                std::snprintf(filter.data(), filter.size(), "%s", local.espSpeciesFilter.c_str());
                if (ImGui::InputText("Species names", filter.data(), filter.size())) {
                    local.espSpeciesFilter = filter.data();
                    changed = true;
                }
            }
            changed |= ImGui::Checkbox("Catchable only", &local.espCatchableOnly);
            changed |= ImGui::Checkbox("Show capture chance", &local.espShowCaptureChance);
            ImGui::BeginDisabled(!local.espShowCaptureChance);
            changed |= ImGui::SliderInt("Minimum chance", &local.espMinCapturePercent, 0, 100, "%d%%",
                                        ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();

            section("Display");
            presentationChanged |= ImGui::Checkbox("Health text", &local.espShowHealthText);
            presentationChanged |= ImGui::Checkbox("Health bar", &local.espShowHealthBar);
            static const char* boxModes[] = { "Off", "Full box", "Corners" };
            presentationChanged |= ImGui::Combo("Box style", &local.espBoxMode, boxModes, IM_ARRAYSIZE(boxModes));
            ImGui::BeginDisabled(local.espBoxMode == 0);
            presentationChanged |= ImGui::Checkbox("Box fill", &local.espBoxFill);
            ImGui::EndDisabled();
            presentationChanged |= ImGui::Checkbox("Snaplines", &local.espSnaplines);
            presentationChanged |= ImGui::Checkbox("Cintilante alert", &local.espShinyAlert);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("INTERCONNECTION")) {
            section("Core");
            changed |= ImGui::Checkbox("God Mode", &local.interconnectionGodMode);
            changed |= ImGui::Checkbox("No Cost", &local.interconnectionNoCost);
            changed |= ImGui::Checkbox("No Cooldown", &local.interconnectionNoCooldown);
            changed |= ImGui::Checkbox("Guaranteed Crit", &local.interconnectionDamage);

            section("Attack");
            changed |= ImGui::Checkbox("ATK Speed", &local.interconnectionAttackSpeed);
            changed |= ImGui::Checkbox("Furious ATK", &local.interconnectionAttackFurious);
            changed |= ImGui::Checkbox("Enhance ATK", &local.interconnectionAttackEnhance);
            changed |= ImGui::Checkbox("Hunt ATK", &local.interconnectionAttackHunt);

            section("Other");
            changed |= ImGui::Checkbox("Capture Rate", &local.interconnectionCaptureBoost);
            changed |= ImGui::Checkbox("Infi Ultimate", &local.interconnectionInfiUltimate);
            ImGui::EndTabItem();
        }

        
        if (ImGui::BeginTabItem("EGG HASTE")) {
            changed |= ImGui::Checkbox("Enable Egg Haste", &local.eggHasteEnabled);
            ImGui::BeginDisabled(!local.eggHasteEnabled);

            if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= ImGui::Checkbox("Egg movement boost", &local.eggHasteEggMovement);
                ImGui::BeginDisabled(!local.eggHasteEggMovement);
                changed |= ImGui::SliderFloat("Egg speed", &local.eggHasteEggSpeedMultiplier, 1.0f, 4.0f, "%.2fx",
                                              ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();

                changed |= ImGui::Checkbox("Egg dash boost", &local.eggHasteDashBoost);
                ImGui::BeginDisabled(!local.eggHasteDashBoost);
                changed |= ImGui::SliderFloat("Dash impulse", &local.eggHasteDashMultiplier, 1.0f, 4.0f, "%.2fx",
                                              ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();
                changed |= ImGui::Checkbox("No dash cooldown", &local.eggHasteNoDashCooldown);

                changed |= ImGui::Checkbox("Fast Carry", &local.eggHasteFastCarryBoost);
                ImGui::BeginDisabled(!local.eggHasteFastCarryBoost);
                changed |= ImGui::SliderFloat("Fast Carry ratio", &local.eggHasteFastCarryRatio, 1.0f, 3.0f, "%.2fx",
                                              ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();
            }

            
            if (ImGui::CollapsingHeader("Fog")) {
                eggAuxChanged |= ImGui::Checkbox("Remove View Fog", &local.eggHasteRemoveViewFog);
                eggAuxChanged |= ImGui::Checkbox("Reveal Full Map", &local.eggHasteRevealFullMap);
            }

            if (ImGui::CollapsingHeader("ESP")) {
                changed |= ImGui::Checkbox("Enable Egg Haste ESP", &local.eggHasteEspEnabled);
                ImGui::BeginDisabled(!local.eggHasteEspEnabled);
                changed |= ImGui::SliderInt("Render distance", &local.eggHasteEspRange, 20, 1000, "%dm",
                                            ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::Checkbox("Eggs", &local.eggHasteEspEggs);
                changed |= ImGui::Checkbox("Egg nests", &local.eggHasteEspNests);
                changed |= ImGui::Checkbox("Resource boxes", &local.eggHasteEspResources);
                changed |= ImGui::Checkbox("Dropped loot", &local.eggHasteEspLoot);
                changed |= ImGui::Checkbox("Player / death bags", &local.eggHasteEspBags);
                changed |= ImGui::Checkbox("Transfer stations", &local.eggHasteEspTransfers);
                changed |= ImGui::Checkbox("Breakables", &local.eggHasteEspBreakables);
                if (ImGui::CollapsingHeader("Chest quality")) {
                    ImGui::BeginDisabled(!local.eggHasteEspResources);
                    eggQualityChanged |= ImGui::Checkbox("White (Q1)", &local.eggHasteQualityWhite);
                    ImGui::SameLine();
                    eggQualityChanged |= ImGui::Checkbox("Green (Q2)", &local.eggHasteQualityGreen);
                    ImGui::SameLine();
                    eggQualityChanged |= ImGui::Checkbox("Blue (Q3)", &local.eggHasteQualityBlue);
                    eggQualityChanged |= ImGui::Checkbox("Purple (Q4)", &local.eggHasteQualityPurple);
                    ImGui::SameLine();
                    eggQualityChanged |= ImGui::Checkbox("Gold (Q5)", &local.eggHasteQualityGold);
                    ImGui::SameLine();
                    eggQualityChanged |= ImGui::Checkbox("Rainbow (Q6)", &local.eggHasteQualityRainbow);
                    eggQualityChanged |= ImGui::Checkbox("Unknown quality", &local.eggHasteQualityUnknown);
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        
        if (ImGui::BeginTabItem("AUTOFARM")) {
            autofarmChanged |= ImGui::Checkbox("Enable Autofarm", &local.autofarmEnabled);
            ImGui::BeginDisabled(!local.autofarmEnabled);
            autofarmChanged |= ImGui::Checkbox("Chests", &local.autofarmIncludeChests);
            autofarmChanged |= ImGui::Checkbox("Lumin Amber", &local.autofarmIncludeAmber);
            autofarmChanged |= ImGui::SliderInt("Live scan radius", &local.autofarmScanRadius, 100, 800, "%dm",
                                                ImGuiSliderFlags_AlwaysClamp);
            local.autofarmEmptyConfirmRadius = std::min(local.autofarmEmptyConfirmRadius, local.autofarmScanRadius);
            autofarmChanged |= ImGui::SliderInt("Empty-area confirmation", &local.autofarmEmptyConfirmRadius, 25,
                                                std::max(25, local.autofarmScanRadius), "%dm",
                                                ImGuiSliderFlags_AlwaysClamp);

            int teleportDelay = static_cast<int>(local.autofarmTeleportDelayMs);
            if (ImGui::SliderInt("Teleport delay", &teleportDelay, 250, 5000, "%d ms", ImGuiSliderFlags_AlwaysClamp)) {
                local.autofarmTeleportDelayMs = static_cast<std::uint32_t>(teleportDelay);
                autofarmChanged = true;
            }
            int settleDelay = static_cast<int>(local.autofarmSettleDelayMs);
            if (ImGui::SliderInt("Streaming settle delay", &settleDelay, 500, 8000, "%d ms", ImGuiSliderFlags_AlwaysClamp)) {
                local.autofarmSettleDelayMs = static_cast<std::uint32_t>(settleDelay);
                autofarmChanged = true;
            }
            ImGui::EndDisabled();

            if (ImGui::Button("Reset Autofarm progress")) {
                ++local.autofarmResetRequest;
                autofarmChanged = true;
            }

            section("Mob Vacuum");
            if (ImGui::Checkbox("Enable Mob Vacuum", &local.mobVacuumEnabled)) {
                autofarmChanged = true;
                if (local.mobVacuumEnabled) local.mobVacuumStun = false;
            }
            if (ImGui::Checkbox("Stun", &local.mobVacuumStun)) {
                autofarmChanged = true;
                if (local.mobVacuumStun) local.mobVacuumEnabled = false;
            }
            autofarmChanged |= ImGui::SliderInt("Vacuum radius", &local.mobVacuumRadius, 1, 500, "%dm",
                                                ImGuiSliderFlags_AlwaysClamp);
            autofarmChanged |= ImGui::SliderInt("Max mobs", &local.mobVacuumMaxMobs, 1, 32, "%d",
                                                ImGuiSliderFlags_AlwaysClamp);
            autofarmChanged |= ImGui::SliderFloat("Distance from player", &local.mobVacuumDistance, 0.5f, 10.0f, "%.1fm",
                                                  ImGuiSliderFlags_AlwaysClamp);
            autofarmChanged |= ImGui::SliderFloat("Reposition tolerance", &local.mobVacuumRepositionTolerance, 0.25f, 10.0f, "%.2fm",
                                                  ImGuiSliderFlags_AlwaysClamp);
            const int vacuumHotkey = mobVacuumHotkeyVk_.load();
            const std::string vacuumLabel = "Bind Mob Vacuum [" + VirtualKeyLabel(vacuumHotkey) + "]";
            if (ImGui::Button(vacuumLabel.c_str())) {
                mobVacuumHotkeyCapture_.store(true);
                mobVacuumHotkeyWasDown_.store(false);
            }
            if (vacuumHotkey > 0 && ImGui::SmallButton("Clear##mob_vacuum_hotkey")) {
                mobVacuumHotkeyVk_.store(0);
                mobVacuumHotkeyWasDown_.store(false);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("AUTOQUEST")) {
            const auto snapshot = ReadSnapshot();
            const NativeAutoquestStats emptyStats{};
            const NativeAutoquestStats& stats = snapshot ? snapshot->autoquestStats : emptyStats;
            const int previewId = local.autoquestSelectedQuestId != 0
                ? local.autoquestSelectedQuestId : stats.selectedQuestId;
            const NativeQuestEntry* previewQuest = nullptr;
            for (const auto& quest : stats.quests) {
                if (quest.id == previewId) {
                    previewQuest = &quest;
                    break;
                }
            }
            const std::string preview = previewQuest
                ? previewQuest->title
                : (local.autoquestSelectedQuestId == 0 ? "Tracked quest" : "Select quest");
            if (ImGui::BeginCombo("Active quest", preview.c_str())) {
                const bool trackedSelected = local.autoquestSelectedQuestId == 0;
                if (ImGui::Selectable("Tracked quest", trackedSelected)) {
                    if (!trackedSelected) {
                        local.autoquestSelectedQuestId = 0;
                        local.autoquestSelectedTemplateId = 0;
                        local.autoquestSelectedMarkId = 0;
                        local.autoquestSelectedMarkRealSceneId = 0;
                        autoquestChanged = true;
                    }
                }
                if (trackedSelected) ImGui::SetItemDefaultFocus();
                for (const auto& quest : stats.quests) {
                    char label[256]{};
                    std::snprintf(label, sizeof(label), "%s [%d]##autoquest_%d",
                                  quest.title.empty() ? "Quest" : quest.title.c_str(), quest.id, quest.id);
                    const bool selected = local.autoquestSelectedQuestId == quest.id;
                    if (ImGui::Selectable(label, selected)) {
                        if (!selected) {
                            local.autoquestSelectedQuestId = quest.id;
                            local.autoquestSelectedTemplateId = 0;
                            local.autoquestSelectedMarkId = 0;
                            local.autoquestSelectedMarkRealSceneId = 0;
                            autoquestChanged = true;
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            autoquestChanged |= ImGui::Checkbox("Autoquest", &local.autoquestEnabled);
            ImGui::SameLine();
            if (ImGui::Button("TP to target")) {
                ++local.autoquestTeleportRequest;
                autoquestChanged = true;
            }
            if (!stats.selectedTitle.empty()) ImGui::Text("Quest: %s", stats.selectedTitle.c_str());
            if (!stats.objective.empty()) ImGui::TextWrapped("Objective: %s", stats.objective.c_str());
            if (stats.targetDistance >= 0.0f) ImGui::Text("Target: %.0f m", stats.targetDistance);
            if (!stats.state.empty() && stats.state != "waiting") ImGui::TextUnformatted(stats.state.c_str());

            if (!stats.petForms.empty()) {
                ImGui::Separator();
                ImGui::Text("Aniimos: %d/%d", stats.petFormsOwned, stats.petFormsRequired);
                for (const auto& pet : stats.petForms) {
                    ImGui::PushID(pet.templateId);
                    const char* stateLabel = pet.missing ? "Falta" : "Obtido";
                    const std::string heading = std::string(pet.name.empty() ? "Aniimo" : pet.name) +
                        "  [" + std::to_string(pet.templateId) + "]  " + stateLabel;
                    if (ImGui::TreeNode(heading.c_str())) {
                        bool foundHabitat = false;
                        for (const auto& habitat : stats.habitats) {
                            if (habitat.templateId != pet.templateId) continue;
                            foundHabitat = true;
                            ImGui::TextWrapped("%s%s%s",
                                habitat.areaName.empty() ? "Area " : habitat.areaName.c_str(),
                                habitat.condition.empty() ? "" : " · ",
                                habitat.condition.c_str());
                            ImGui::SameLine();
                            if (habitat.markId != 0) {
                                const std::string buttonLabel = "TP" +
                                    std::string(habitat.areaFallback ? " area" : "") +
                                    "##aq_tp_" + std::to_string(pet.templateId) + "_" +
                                    std::to_string(habitat.smallAreaId) + "_" + std::to_string(habitat.markId);
                                if (ImGui::SmallButton(buttonLabel.c_str())) {
                                    local.autoquestSelectedTemplateId = pet.templateId;
                                    local.autoquestSelectedMarkId = habitat.markId;
                                    local.autoquestSelectedMarkRealSceneId = habitat.markRealSceneId;
                                    ++local.autoquestTeleportRequest;
                                    autoquestChanged = true;
                                }
                                if (!habitat.markName.empty()) {
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(habitat.markName.c_str());
                                }
                            } else {
                                ImGui::SameLine();
                                ImGui::TextDisabled("Sem TP desbloqueado");
                            }
                        }
                        if (!foundHabitat) ImGui::TextDisabled("Habitat indisponivel");
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("DISABLE ALL", ImVec2(-1.0f, 0.0f))) {
        DisableAll();
        local = ReadControls();
        changed = false;
        presentationChanged = false;
        eggAuxChanged = false;
        eggQualityChanged = false;
        autofarmChanged = false;
        autoquestChanged = false;
    }

    if (changed || presentationChanged || eggAuxChanged || eggQualityChanged || autofarmChanged || autoquestChanged || profileApplied) {
        std::lock_guard lock(controlsMutex_);
        local.revision = (changed || profileApplied) ? (controls_.revision + 1) : controls_.revision;
        controls_ = local;
    }

    ImGui::End();
}
void NativeOverlay::RenderEsp() {
    const NativeUiControls controls = ReadControls();
    const bool eggEspEnabled = controls.eggHasteEnabled && controls.eggHasteEspEnabled;
    if (!controls.espEnabled && !eggEspEnabled) return;

    const auto snapshot = ReadSnapshot();
    if (!snapshot || snapshot->sourceWidth <= 0.0f || snapshot->sourceHeight <= 0.0f) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x <= 0.0f || display.y <= 0.0f) return;

    const float scaleX = display.x / snapshot->sourceWidth;
    const float scaleY = display.y / snapshot->sourceHeight;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    struct VisibleEspItem {
        const NativeEspItem* item{};
        float x{};
        float y{};
        float topX{};
        float topY{};
        bool hasTop{};
    };
    std::vector<VisibleEspItem> visible;
    visible.reserve(snapshot->items.size());
    for (const auto& item : snapshot->items) {
        float maxDistance = 0.0f;
        if (IsEggHasteKind(item.kind)) {
            if (!eggEspEnabled || !snapshot->eggHasteStats.liveSpace || !EggHasteKindEnabled(controls, item.kind)) continue;
            maxDistance = static_cast<float>(controls.eggHasteEspRange);
        } else {
            if (!controls.espEnabled || !KindEnabled(controls, item.kind)) continue;
            if (IsCreatureKind(item.kind)) {
                if (!CreatureLabelEnabled(controls, item.labelMask)) continue;
                if (controls.espCatchableOnly && !item.catchable) continue;
                if (controls.espMinCapturePercent > 0 &&
                    (item.captureChance < 0.0f || item.captureChance + 0.001f < static_cast<float>(controls.espMinCapturePercent))) continue;
            }
            maxDistance = static_cast<float>(controls.espRange);
        }
        if (item.distance < 0.0f || item.distance > maxDistance) continue;

        const float sx = item.screenX * scaleX;
        const float sy = display.y - item.screenY * scaleY;
        if (sx < -24.0f || sx > display.x + 24.0f || sy < -24.0f || sy > display.y + 24.0f) continue;

        VisibleEspItem next{&item, sx, sy, 0.0f, 0.0f, false};
        if (item.topDepth > 0.0f && item.topScreenX >= 0.0f && item.topScreenY >= 0.0f) {
            next.topX = item.topScreenX * scaleX;
            next.topY = display.y - item.topScreenY * scaleY;
            const float projectedHeight = std::abs(next.y - next.topY);
            next.hasTop = projectedHeight >= 6.0f && projectedHeight <= display.y * 1.25f;
        }
        visible.push_back(next);
    }

    std::sort(visible.begin(), visible.end(), [](const VisibleEspItem& a, const VisibleEspItem& b) {
        return a.item->distance > b.item->distance;
    });

    const VisibleEspItem* shinyAlertItem = nullptr;
    if (controls.espEnabled && controls.espShinyAlert) {
        for (const auto& candidate : visible) {
            if (IsEggHasteKind(candidate.item->kind) || !IsCreatureKind(candidate.item->kind) ||
                (candidate.item->labelMask & kPetLabelShiny) == 0u) continue;
            if (!shinyAlertItem || candidate.item->distance < shinyAlertItem->item->distance)
                shinyAlertItem = &candidate;
        }
    }

    constexpr float markerRadius = 3.5f;
    constexpr float labelGap = 8.0f;
    constexpr float labelPadX = 5.0f;
    constexpr float labelPadY = 3.0f;
    constexpr float edgePad = 4.0f;

    for (const VisibleEspItem& visibleItem : visible) {
        const NativeEspItem& item = *visibleItem.item;
        const float sx = visibleItem.x;
        const float sy = visibleItem.y;
        const bool mainCreature = !IsEggHasteKind(item.kind) && IsCreatureKind(item.kind);
        const bool shiny = mainCreature && (item.labelMask & kPetLabelShiny) != 0u;
        const ImU32 baseColor = (item.kind == "egg-haste-resource" && item.quality > 0)
                                    ? EggChestColor(item.quality) : EspColor(item.kind);
        const ImU32 color = (controls.espShinyAlert && shiny) ? IM_COL32(255, 200, 64, 250) : baseColor;

        const bool validHealth = mainCreature && item.curHp >= 0.0f && item.maxHp > 0.0f;
        const float healthRatio = validHealth ? std::clamp(item.curHp / item.maxHp, 0.0f, 1.0f) : 0.0f;

        if (controls.espSnaplines && mainCreature) {
            draw->AddLine(ImVec2(display.x * 0.5f, display.y - 2.0f), ImVec2(sx, sy),
                          WithAlpha(color, 0.42f), 1.0f);
        }

        bool drewEntityBox = false;
        ImVec2 entityBoxMin{};
        ImVec2 entityBoxMax{};
        if (mainCreature && controls.espBoxMode != 0 && visibleItem.hasTop) {
            const float topY = std::min(visibleItem.topY, sy);
            const float bottomY = std::max(visibleItem.topY, sy);
            const float height = bottomY - topY;
            if (height >= 6.0f) {
                const float centerX = (visibleItem.topX + sx) * 0.5f;
                const float width = std::clamp(height * 0.55f, 12.0f, 220.0f);
                entityBoxMin = ImVec2(centerX - width * 0.5f, topY - 2.0f);
                entityBoxMax = ImVec2(centerX + width * 0.5f, bottomY + 2.0f);
                if (controls.espBoxFill)
                    draw->AddRectFilled(entityBoxMin, entityBoxMax, WithAlpha(color, 0.08f));
                if (controls.espBoxMode == 1) {
                    draw->AddRect(entityBoxMin, entityBoxMax, IM_COL32(6, 8, 12, 210), 0.0f, 0, 3.0f);
                    draw->AddRect(entityBoxMin, entityBoxMax, color, 0.0f, 0, 1.4f);
                } else {
                    DrawCornerBox(draw, entityBoxMin, entityBoxMax, IM_COL32(6, 8, 12, 220), 3.0f);
                    DrawCornerBox(draw, entityBoxMin, entityBoxMax, color, 1.4f);
                }
                drewEntityBox = true;
            }
        }

        char label[320]{};
        const char* name = item.name.empty() ? item.kind.c_str() : item.name.c_str();
        const bool isCaptureTarget = !IsEggHasteKind(item.kind) && !item.actorId.empty() && item.actorId == snapshot->captureStats.targetActorId;
        const char* targetPrefix = isCaptureTarget ? "> " : "";
        if (item.kind == "egg-haste-resource" && !item.value.empty()) {
            std::snprintf(label, sizeof(label), "%s%s%s  %.0fm", targetPrefix, name, item.value.c_str(), item.distance);
        } else if (controls.espShowCaptureChance && item.captureChance >= 0.0f && mainCreature) {
            if (item.level > 0) {
                std::snprintf(label, sizeof(label), "%s%s  Lv%d  %.0fm  [%.0f%%]", targetPrefix, name, item.level,
                              item.distance, item.captureChance);
            } else {
                std::snprintf(label, sizeof(label), "%s%s  %.0fm  [%.0f%%]", targetPrefix, name, item.distance,
                              item.captureChance);
            }
        } else if (item.level > 0) {
            std::snprintf(label, sizeof(label), "%s%s  Lv%d  %.0fm", targetPrefix, name, item.level, item.distance);
        } else {
            std::snprintf(label, sizeof(label), "%s%s  %.0fm", targetPrefix, name, item.distance);
        }

        std::string labelText(label);
        if (controls.espShowHealthText && validHealth) {
            char healthText[72]{};
            std::snprintf(healthText, sizeof(healthText), "  HP %.0f/%.0f", item.curHp, item.maxHp);
            labelText += healthText;
        }
        if (controls.espShinyAlert && shiny) labelText = std::string("[CINTILANTE] ") + labelText;

        const ImVec2 textSize = ImGui::CalcTextSize(labelText.c_str());
        ImVec2 textPos;
        if (drewEntityBox) {
            textPos = ImVec2(entityBoxMax.x + labelGap, entityBoxMin.y);
        } else {
            textPos = ImVec2(sx + labelGap, sy - textSize.y * 0.5f);
        }
        textPos.x = std::clamp(textPos.x, edgePad, std::max(edgePad, display.x - textSize.x - labelPadX * 2.0f - edgePad));
        textPos.y = std::clamp(textPos.y, edgePad, std::max(edgePad, display.y - textSize.y - labelPadY * 2.0f - edgePad));

        const ImVec2 labelBoxMin(textPos.x - labelPadX, textPos.y - labelPadY);
        const ImVec2 labelBoxMax(textPos.x + textSize.x + labelPadX, textPos.y + textSize.y + labelPadY);

        draw->AddCircleFilled(ImVec2(sx, sy), markerRadius + 2.0f, IM_COL32(8, 10, 14, 220), 12);
        draw->AddCircleFilled(ImVec2(sx, sy), markerRadius, color, 12);
        draw->AddRectFilled(labelBoxMin, labelBoxMax, IM_COL32(8, 10, 14, 205), 4.0f);
        const std::size_t valueStart = item.kind == "egg-haste-resource" && !item.value.empty()
                                           ? labelText.find(item.value) : std::string::npos;
        if (valueStart != std::string::npos) {
            const std::string prefix = labelText.substr(0, valueStart);
            const std::string suffix = labelText.substr(valueStart + item.value.size());
            draw->AddText(textPos, color, prefix.c_str());
            const float prefixWidth = ImGui::CalcTextSize(prefix.c_str()).x;
            const ImU32 valueColor = item.value == "?" ? color : IM_COL32(255, 82, 82, 255);
            draw->AddText(ImVec2(textPos.x + prefixWidth, textPos.y), valueColor, item.value.c_str());
            const float valueWidth = ImGui::CalcTextSize(item.value.c_str()).x;
            draw->AddText(ImVec2(textPos.x + prefixWidth + valueWidth, textPos.y), color, suffix.c_str());
        } else {
            draw->AddText(textPos, color, labelText.c_str());
        }

        if (controls.espShowHealthBar && validHealth) {
            ImVec2 barMin;
            ImVec2 barMax;
            if (drewEntityBox) {
                barMin = ImVec2(entityBoxMin.x, entityBoxMax.y + 4.0f);
                barMax = ImVec2(entityBoxMax.x, entityBoxMax.y + 8.0f);
            } else {
                barMin = ImVec2(labelBoxMin.x, labelBoxMax.y + 2.0f);
                barMax = ImVec2(labelBoxMax.x, labelBoxMax.y + 6.0f);
            }
            draw->AddRectFilled(barMin, barMax, IM_COL32(8, 10, 14, 225), 2.0f);
            const float width = std::max(0.0f, barMax.x - barMin.x);
            const ImVec2 fillMax(barMin.x + width * healthRatio, barMax.y);
            draw->AddRectFilled(barMin, fillMax, color, 2.0f);
        }
    }

    if (shinyAlertItem) {
        const NativeEspItem& shinyItem = *shinyAlertItem->item;
        char alert[320]{};
        const char* shinyName = shinyItem.name.empty() ? "Aniimo" : shinyItem.name.c_str();
        std::snprintf(alert, sizeof(alert), "CINTILANTE: %s  %.0fm", shinyName, shinyItem.distance);
        const ImVec2 textSize = ImGui::CalcTextSize(alert);
        const ImVec2 center(display.x * 0.5f, 38.0f);
        const ImVec2 min(center.x - textSize.x * 0.5f - 12.0f, center.y - 7.0f);
        const ImVec2 max(center.x + textSize.x * 0.5f + 12.0f, center.y + textSize.y + 7.0f);
        const ImU32 gold = IM_COL32(255, 200, 64, 255);
        draw->AddRectFilled(min, max, IM_COL32(8, 10, 14, 225), 5.0f);
        draw->AddRect(min, max, gold, 5.0f, 0, 1.5f);
        draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y), gold, alert);
    }
}

} // namespace arkhan
