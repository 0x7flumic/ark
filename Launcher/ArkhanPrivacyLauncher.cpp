#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Shellapi.h>
#include <Shobjidl.h>
#include <Objbase.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace {

constexpr std::string_view kPrivacyBegin =
    "# ARKHAN_PRIVACY_GUARD_BEGIN";
constexpr std::string_view kPrivacyEnd =
    "# ARKHAN_PRIVACY_GUARD_END";

// Name of the firewall rule managed by Arkhan
constexpr wchar_t kFirewallRuleName[] = L"ArkhanBlockTcp80Out";

// Combined list with all 6 current blocking rules
constexpr std::array<std::string_view, 6> kPrivacyEntries{{
    "0.0.0.0 fp-logagent-worldx.kingsgroupgames.com",
    "0.0.0.0 sentry.prod.ops.butterfly-effect.dev",
    "0.0.0.0 ir-sdk.guardease.com",
    "0.0.0.0 fp-toolskit.funplus.com",
    "0.0.0.0 priv-platform-yimoo-api.funplus.com",
    "0.0.0.0 fp-privacy.funplus.com"
}};

// Legacy block kept to allow clean removal of old installations
constexpr std::string_view kLegacyBegin =
    "# ARKHAN_NETWORK_GUARD_BEGIN";
constexpr std::string_view kLegacyEnd =
    "# ARKHAN_NETWORK_GUARD_END";
constexpr std::array<std::string_view, 6> kLegacyEntries{{
    "0.0.0.0 fp-logagent-worldx.kingsgroupgames.com",
    "0.0.0.0 sentry.prod.ops.butterfly-effect.dev",
    "0.0.0.0 ir-sdk.guardease.com",
    "0.0.0.0 fp-toolskit.funplus.com",
    "0.0.0.0 priv-platform-yimoo-api.funplus.com",
    "0.0.0.0 fp-privacy.funplus.com"
}};

struct Line {
    std::string_view text;
    std::size_t start{};
    std::size_t finish{};
};

struct Block {
    std::size_t start{};
    std::size_t finish{};
};

enum class BlockStatus {
    Absent,
    Valid,
    Malformed
};

std::vector<Line> SplitLines(const std::string& content) {
    std::vector<Line> lines;
    std::size_t pos = 0;

    while (pos < content.size()) {
        const auto separator = content.find('\n', pos);
        const auto finish = separator == std::string::npos
            ? content.size()
            : separator + 1;

        auto textFinish = separator == std::string::npos
            ? finish
            : separator;

        if (textFinish > pos && content[textFinish - 1] == '\r') {
            --textFinish;
        }

        auto contentStart = pos;
        if (pos == 0 && textFinish >= 3 &&
            static_cast<unsigned char>(content[0]) == 0xEF &&
            static_cast<unsigned char>(content[1]) == 0xBB &&
            static_cast<unsigned char>(content[2]) == 0xBF) {
            contentStart = 3;
        }

        lines.push_back({
            std::string_view(content).substr(
                contentStart, textFinish - contentStart),
            contentStart,
            finish
        });

        pos = finish;
    }

    return lines;
}

template <std::size_t N>
BlockStatus InspectBlock(
    const std::string& content,
    std::string_view beginMarker,
    const std::array<std::string_view, N>& expectedEntries,
    std::string_view endMarker,
    Block* output) {

    const auto lines = SplitLines(content);
    const auto notFound = lines.size();
    std::size_t beginIndex = notFound;
    std::size_t endIndex = notFound;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].text == beginMarker) {
            if (beginIndex != notFound) {
                return BlockStatus::Malformed;
            }
            beginIndex = i;
        }

        if (lines[i].text == endMarker) {
            if (endIndex != notFound) {
                return BlockStatus::Malformed;
            }
            endIndex = i;
        }
    }

    if (beginIndex == notFound && endIndex == notFound) {
        return BlockStatus::Absent;
    }

    if (beginIndex == notFound || endIndex == notFound ||
        endIndex != beginIndex + N + 1) {
        return BlockStatus::Malformed;
    }

    for (std::size_t i = 0; i < N; ++i) {
        if (lines[beginIndex + 1 + i].text != expectedEntries[i]) {
            return BlockStatus::Malformed;
        }
    }

    if (output != nullptr) {
        output->start = lines[beginIndex].start;
        output->finish = lines[endIndex].finish;
    }

    return BlockStatus::Valid;
}

bool ReadAll(const std::filesystem::path& path, std::string* result) {
    if (result == nullptr) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(1024 * 1024)) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file) {
        return false;
    }

    result->assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());

    return file.good() || file.eof();
}

std::filesystem::path HostsPath() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const auto count = GetWindowsDirectoryW(
        buffer.data(), static_cast<UINT>(buffer.size()));

    if (count == 0 || count >= static_cast<UINT>(buffer.size())) {
        return {};
    }

    return std::filesystem::path(buffer.data()) /
        L"System32" / L"drivers" / L"etc" / L"hosts";
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    const BOOL ok = GetTokenInformation(
        token, TokenElevation, &elevation, sizeof(elevation), &returned);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool ReplaceHosts(const std::filesystem::path& hosts,
                  const std::string& updated) {
    std::array<wchar_t, MAX_PATH> temporary{};
    const auto parent = hosts.parent_path().wstring();

    if (!GetTempFileNameW(parent.c_str(), L"akh", 0, temporary.data())) {
        return false;
    }

    const std::filesystem::path pending(temporary.data());
    bool written = false;
    {
        std::ofstream file(pending, std::ios::binary | std::ios::trunc);
        if (file) {
            file.write(updated.data(), static_cast<std::streamsize>(updated.size()));
            file.flush();
            written = file.good();
        }
    }

    if (!written) {
        DeleteFileW(pending.c_str());
        return false;
    }

    const BOOL replaced = ReplaceFileW(
        hosts.c_str(), pending.c_str(), nullptr,
        REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);

    if (!replaced) {
        DeleteFileW(pending.c_str());
    }

    return replaced != FALSE;
}

bool RemoveValidatedBlock(std::string* content,
                          std::string_view beginMarker,
                          const std::array<std::string_view, 6>& entries,
                          std::string_view endMarker,
                          bool* removed,
                          std::string* error) {
    Block block{};
    const auto status = InspectBlock(
        *content, beginMarker, entries, endMarker, &block);

    if (status == BlockStatus::Malformed) {
        *error = "An existing Arkhan block is altered or incomplete; no changes were made.";
        return false;
    }

    *removed = status == BlockStatus::Valid;
    if (*removed) {
        content->erase(block.start, block.finish - block.start);
    }
    return true;
}

bool RemovePrivacyBlock(std::string* content,
                        bool* removed,
                        std::string* error) {
    Block block{};
    const auto status = InspectBlock(
        *content, kPrivacyBegin, kPrivacyEntries, kPrivacyEnd, &block);

    if (status == BlockStatus::Malformed) {
        *error = "The temporary privacy block was modified; for safety, it was not automatically removed.";
        return false;
    }

    *removed = status == BlockStatus::Valid;
    if (*removed) {
        content->erase(block.start, block.finish - block.start);
    }
    return true;
}

bool HasAnyManualMapping(const std::string& content,
                         std::string_view targetDomain) {
    for (const auto& line : SplitLines(content)) {
        auto value = line.text;
        const auto comment = value.find('#');
        if (comment != std::string_view::npos) {
            value = value.substr(0, comment);
        }

        std::string normalized(value);
        std::transform(
            normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

        std::size_t pos = 0;
        std::vector<std::string_view> tokens;
        while (pos < normalized.size()) {
            while (pos < normalized.size() &&
                   std::isspace(static_cast<unsigned char>(normalized[pos]))) {
                ++pos;
            }
            if (pos == normalized.size()) {
                break;
            }
            const auto begin = pos;
            while (pos < normalized.size() &&
                   !std::isspace(static_cast<unsigned char>(normalized[pos]))) {
                ++pos;
            }
            tokens.emplace_back(normalized.data() + begin, pos - begin);
        }

        if (tokens.size() < 2) {
            continue;
        }

        for (std::size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == targetDomain) {
                return true;
            }
        }
    }

    return false;
}

std::string AppendPrivacyBlock(const std::string& original) {
    const std::string newline =
        original.find("\r\n") != std::string::npos ? "\r\n" : "\n";

    std::string updated = original;
    updated.reserve(original.size() + 256);

    if (!updated.empty() && updated.back() != '\n') {
        updated += newline;
    }

    updated += kPrivacyBegin;
    updated += newline;
    for (const auto& entry : kPrivacyEntries) {
        updated += entry;
        updated += newline;
    }
    updated += kPrivacyEnd;
    updated += newline;
    return updated;
}

// Functions to manage the firewall rule via Netsh (requires administrator privileges)
bool CreateFirewallRule() {
    std::wstring cmd = L"netsh advfirewall firewall add rule name=\"" + std::wstring(kFirewallRuleName) +
                       L"\" dir=out action=block protocol=TCP remoteport=80";
    _wsystem(cmd.c_str());
    return true;
}

bool RemoveFirewallRule() {
    std::wstring cmd = L"netsh advfirewall firewall delete rule name=\"" + std::wstring(kFirewallRuleName) + L"\"";
    _wsystem(cmd.c_str());
    return true;
}

std::filesystem::path StatusPathForPid(DWORD pid) {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD count = GetTempPathW(
        static_cast<DWORD>(buffer.size()), buffer.data());
    if (count == 0 || count >= buffer.size()) {
        return {};
    }

    std::wstring name = L"ArkhanPrivacyGuard_";
    name += std::to_wstring(pid);
    name += L".status";
    return std::filesystem::path(buffer.data()) / name;
}

std::wstring RestoreEventName(DWORD pid) {
    return L"Local\\ArkhanPrivacyRestore_" + std::to_wstring(pid);
}

bool WriteStatus(const std::filesystem::path& path,
                 const std::string& status) {
    if (path.empty()) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    file.write(status.data(), static_cast<std::streamsize>(status.size()));
    file.flush();
    return file.good();
}

bool ReadStatus(const std::filesystem::path& path, std::string* status) {
    if (status == nullptr || path.empty()) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    status->assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return file.good() || file.eof();
}

bool ApplyTemporaryRule(std::string* statusMessage) {
    const auto hosts = HostsPath();
    if (hosts.empty()) {
        *statusMessage = "ERROR: Could not locate the Windows hosts file.";
        return false;
    }

    std::string original;
    if (!ReadAll(hosts, &original)) {
        *statusMessage = "ERROR: Could not read the hosts file.";
        return false;
    }
    if (original.find('\0') != std::string::npos) {
        *statusMessage = "ERROR: The hosts file has an unsupported encoding.";
        return false;
    }

    std::string updated = original;
    std::string error;
    bool legacyRemoved = false;
    bool privacyRemoved = false;

    if (!RemoveValidatedBlock(
            &updated, kLegacyBegin, kLegacyEntries, kLegacyEnd,
            &legacyRemoved, &error)) {
        *statusMessage = "ERROR: " + error;
        return false;
    }

    if (!RemovePrivacyBlock(&updated, &privacyRemoved, &error)) {
        *statusMessage = "ERROR: " + error;
        return false;
    }

    bool manuallyMapped = HasAnyManualMapping(updated, "sentry.prod.ops.butterfly-effect.dev");
    if (!manuallyMapped) {
        updated = AppendPrivacyBlock(updated);
    }

    if (updated != original && !ReplaceHosts(hosts, updated)) {
        *statusMessage = "ERROR: Could not update the hosts file. Run the launcher again as administrator.";
        return false;
    }

    // Creates the firewall rule to block outgoing port 80
    CreateFirewallRule();

    std::string message = "READY: ";
    if (manuallyMapped) {
        message += "The Sentry endpoint already has a manual mapping; it was preserved.";
    } else {
        message += "bypass in progress...";
    }
    if (legacyRemoved) {
        message += " The old ARKHAN_NETWORK_GUARD block was removed.";
    }
    message += " bypass completed.";
    *statusMessage = message;
    return true;
}

bool RestoreTemporaryRule(std::string* statusMessage) {
    const auto hosts = HostsPath();
    if (hosts.empty()) {
        *statusMessage = "RESTORE_ERROR: Could not locate the hosts file.";
        return false;
    }

    std::string current;
    if (!ReadAll(hosts, &current)) {
        *statusMessage = "RESTORE_ERROR: Could not re-read the hosts file.";
        return false;
    }
    if (current.find('\0') != std::string::npos) {
        *statusMessage = "RESTORE_ERROR: The hosts file has an unsupported encoding.";
        return false;
    }

    std::string updated = current;
    std::string error;
    bool removed = false;
    if (!RemovePrivacyBlock(&updated, &removed, &error)) {
        *statusMessage = "RESTORE_ERROR: " + error;
        return false;
    }

    if (removed && !ReplaceHosts(hosts, updated)) {
        *statusMessage = "RESTORE_ERROR: Could not remove temporary rules from hosts.";
        return false;
    }

    // Removes the Windows firewall rule upon exit
    RemoveFirewallRule();

    *statusMessage = removed
        ? "RESTORED: Arkhan temporary rules and firewall were removed."
        : "RESTORED: No managed temporary rules were found, but the firewall was cleaned up.";
    return true;
}

int RunElevatedHelper(DWORD parentPid) {
    const auto statusPath = StatusPathForPid(parentPid);
    const auto eventName = RestoreEventName(parentPid);

    HANDLE parentProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    HANDLE restoreEvent = OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());

    if (parentProcess == nullptr || restoreEvent == nullptr) {
        WriteStatus(statusPath,
            "ERROR: Could not connect the helper to the main launcher.");
        if (parentProcess != nullptr) CloseHandle(parentProcess);
        if (restoreEvent != nullptr) CloseHandle(restoreEvent);
        return 10;
    }

    if (!IsElevated()) {
        WriteStatus(statusPath,
            "ERROR: The helper did not receive administrative privileges.");
        CloseHandle(parentProcess);
        CloseHandle(restoreEvent);
        return 11;
    }

    std::string statusMessage;
    if (!ApplyTemporaryRule(&statusMessage)) {
        WriteStatus(statusPath, statusMessage);
        CloseHandle(parentProcess);
        CloseHandle(restoreEvent);
        return 12;
    }

    WriteStatus(statusPath, statusMessage);

    HANDLE waits[] = { parentProcess, restoreEvent };
    WaitForMultipleObjects(2, waits, FALSE, INFINITE);

    std::string restoreMessage;
    const bool restored = RestoreTemporaryRule(&restoreMessage);
    WriteStatus(statusPath, restoreMessage);

    CloseHandle(parentProcess);
    CloseHandle(restoreEvent);
    return restored ? 0 : 13;
}

std::wstring GetSelfPath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), count);
}

std::optional<std::filesystem::path> PickExecutable() {
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninitialize = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (mustUninitialize) CoUninitialize();
        return std::nullopt;
    }

    const COMDLG_FILTERSPEC filters[] = {
        { L"Executables (*.exe)", L"*.exe" },
        { L"All files (*.*)", L"*.*" }
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);

    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_FILEMUSTEXIST |
            FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    }

    std::optional<std::filesystem::path> selected;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR filePath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath)) &&
                filePath != nullptr) {
                selected = std::filesystem::path(filePath);
                CoTaskMemFree(filePath);
            }
            item->Release();
        }
    }

    dialog->Release();
    if (mustUninitialize) CoUninitialize();
    return selected;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;

    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += L'\"';
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += ch;
    }

    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
}

bool StartElevatedHelper(DWORD parentPid,
                         HANDLE restoreEvent,
                         HANDLE* helperProcess,
                         const std::filesystem::path& statusPath,
                         std::string* errorMessage) {
    (void)restoreEvent;
    const auto selfPath = GetSelfPath();
    if (selfPath.empty()) {
        *errorMessage = "Could not locate the launcher executable.";
        return false;
    }

    if (!statusPath.empty()) {
        DeleteFileW(statusPath.c_str());
    }

    const std::wstring parameters =
        L"--helper " + std::to_wstring(parentPid);
    const std::filesystem::path self(selfPath);
    const auto directory = self.parent_path().wstring();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = GetConsoleWindow();
    info.lpVerb = L"runas";
    info.lpFile = selfPath.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            *errorMessage = "The administrator request was cancelled.";
        } else {
            *errorMessage = "Could not start the elevated helper. Windows error code: " +
                std::to_string(error);
        }
        return false;
    }

    *helperProcess = info.hProcess;
    return *helperProcess != nullptr;
}

bool WaitForHelperReady(HANDLE helperProcess,
                        const std::filesystem::path& statusPath,
                        std::string* statusMessage) {
    const ULONGLONG deadline = GetTickCount64() + 30000;

    while (GetTickCount64() < deadline) {
        std::string status;
        if (ReadStatus(statusPath, &status)) {
            if (status.rfind("READY:", 0) == 0) {
                *statusMessage = status;
                return true;
            }
            if (status.rfind("ERROR:", 0) == 0) {
                *statusMessage = status;
                return false;
            }
        }

        const DWORD wait = WaitForSingleObject(helperProcess, 100);
        if (wait == WAIT_OBJECT_0) {
            if (ReadStatus(statusPath, &status)) {
                *statusMessage = status;
            } else {
                *statusMessage = "The helper finished before confirming hosts modification.";
            }
            return false;
        }
        if (wait == WAIT_FAILED) {
            *statusMessage = "Failed to wait for the administrative helper.";
            return false;
        }
    }

    *statusMessage = "Timeout waiting for temporary hosts configuration.";
    return false;
}

void SignalRestoreAndWait(HANDLE restoreEvent,
                          HANDLE helperProcess,
                          const std::filesystem::path& statusPath) {
    if (restoreEvent != nullptr) {
        SetEvent(restoreEvent);
    }

    if (helperProcess != nullptr) {
        const DWORD wait = WaitForSingleObject(helperProcess, 15000);
        if (wait == WAIT_OBJECT_0) {
            std::string status;
            if (ReadStatus(statusPath, &status)) {
                std::cout << status << "\n";
            }
        } else {
            std::cout << "The helper is still finishing the restoration.\n";
        }
    }
}

int RunLauncher(const std::filesystem::path& executable,
                const std::vector<std::wstring>& gameArguments) {
    std::error_code ec;
    const auto absoluteExecutable = std::filesystem::absolute(executable, ec);
    if (ec || !std::filesystem::exists(absoluteExecutable, ec) || ec ||
        !std::filesystem::is_regular_file(absoluteExecutable, ec) || ec) {
        std::cerr << "The selected executable does not exist or cannot be accessed.\n";
        return 2;
    }

    // Source directories (where the launcher runs) and destination (game directory)
    const auto launcherDir = std::filesystem::path(GetSelfPath()).parent_path();
    const auto gameDir = absoluteExecutable.parent_path();

    const auto srcDll = launcherDir / L"version.dll";
    const auto srcArkhan = launcherDir / L"Arkhan";

    const auto destDll = gameDir / L"version.dll";
    const auto destArkhan = gameDir / L"Arkhan";

    bool movedDll = false;
    bool movedArkhan = false;

    // Temporarily moves version.dll and the Arkhan folder to the game directory
    if (std::filesystem::exists(srcDll, ec)) {
        if (std::filesystem::exists(destDll, ec)) {
            std::filesystem::remove(destDll, ec);
        }
        std::filesystem::rename(srcDll, destDll, ec);
        if (!ec) movedDll = true;
    }

    if (std::filesystem::exists(srcArkhan, ec)) {
        if (std::filesystem::exists(destArkhan, ec)) {
            std::filesystem::remove_all(destArkhan, ec);
        }
        std::filesystem::rename(srcArkhan, destArkhan, ec);
        if (!ec) movedArkhan = true;
    }

    // Lambda function to ensure files return to the launcher folder upon exit
    auto restoreFilesToLauncher = [&]() {
        std::error_code ecCleanup;
        if (movedDll && std::filesystem::exists(destDll, ecCleanup)) {
            if (std::filesystem::exists(srcDll, ecCleanup)) {
                std::filesystem::remove(srcDll, ecCleanup);
            }
            std::filesystem::rename(destDll, srcDll, ecCleanup);
        }
        if (movedArkhan && std::filesystem::exists(destArkhan, ecCleanup)) {
            if (std::filesystem::exists(srcArkhan, ecCleanup)) {
                std::filesystem::remove_all(srcArkhan, ecCleanup);
            }
            std::filesystem::rename(destArkhan, srcArkhan, ecCleanup);
        }
    };

    const DWORD pid = GetCurrentProcessId();
    const auto statusPath = StatusPathForPid(pid);
    if (statusPath.empty()) {
        std::cerr << "Could not create temporary status path.\n";
        restoreFilesToLauncher();
        return 3;
    }

    const auto eventName = RestoreEventName(pid);
    SetLastError(ERROR_SUCCESS);
    HANDLE restoreEvent = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (restoreEvent == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (restoreEvent != nullptr) CloseHandle(restoreEvent);
        std::cerr << "Could not prepare restoration event.\n";
        restoreFilesToLauncher();
        return 4;
    }

    HANDLE helperProcess = nullptr;
    std::string errorMessage;
    if (!StartElevatedHelper(pid, restoreEvent, &helperProcess,
                           statusPath, &errorMessage)) {
        std::cerr << errorMessage << "\n";
        CloseHandle(restoreEvent);
        restoreFilesToLauncher();
        return 5;
    }

    std::string statusMessage;
    if (!WaitForHelperReady(helperProcess, statusPath, &statusMessage)) {
        std::cerr << statusMessage << "\n";
        SignalRestoreAndWait(restoreEvent, helperProcess, statusPath);
        CloseHandle(helperProcess);
        CloseHandle(restoreEvent);
        DeleteFileW(statusPath.c_str());
        restoreFilesToLauncher();
        return 6;
    }

    std::cout << statusMessage << "\n";
    std::cout << "Starting the game.\n";

    std::wstring commandLine = QuoteArgument(absoluteExecutable.wstring());
    for (const auto& argument : gameArguments) {
        commandLine += L" ";
        commandLine += QuoteArgument(argument);
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const auto workingDirectory = absoluteExecutable.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    const BOOL created = CreateProcessW(
        absoluteExecutable.c_str(),
        mutableCommand.data(),
        nullptr, nullptr, FALSE, 0, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup, &process);

    if (!created) {
        const DWORD error = GetLastError();
        std::cerr << "Could not start the game. Windows error code: "
                  << error << "\n";
        SignalRestoreAndWait(restoreEvent, helperProcess, statusPath);
        CloseHandle(helperProcess);
        CloseHandle(restoreEvent);
        DeleteFileW(statusPath.c_str());
        restoreFilesToLauncher();
        return 7;
    }

    CloseHandle(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD gameExitCode = 0;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &gameExitCode);
    } else {
        std::cerr << "Failed to wait for the game process.\n";
    }
    CloseHandle(process.hProcess);

    SignalRestoreAndWait(restoreEvent, helperProcess, statusPath);

    CloseHandle(helperProcess);
    CloseHandle(restoreEvent);

    std::string finalStatus;
    if (ReadStatus(statusPath, &finalStatus) &&
        finalStatus.rfind("RESTORE_ERROR:", 0) == 0) {
        std::cerr << "WARNING: " << finalStatus << "\n";
        DeleteFileW(statusPath.c_str());
        restoreFilesToLauncher();
        return 8;
    }

    DeleteFileW(statusPath.c_str());
    
    // Return the files to the launcher folder after closing the game successfully
    restoreFilesToLauncher();

    std::cout << "Game closed. Restoration completed. Exit code: "
              << gameExitCode << "\n";
    return waitResult == WAIT_OBJECT_0 ? static_cast<int>(gameExitCode) : 9;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 3 && std::wstring_view(argv[1]) == L"--helper") {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(argv[2], &end, 10);
        if (end == argv[2] || *end != L'\0' || parsed == 0 ||
            parsed > static_cast<unsigned long>(MAXDWORD)) {
            return 20;
        }
        return RunElevatedHelper(static_cast<DWORD>(parsed));
    }

    std::filesystem::path gameExecutable;
    std::vector<std::wstring> gameArguments;

    if (argc >= 2) {
        gameExecutable = argv[1];
        for (int i = 2; i < argc; ++i) {
            gameArguments.emplace_back(argv[i]);
        }
    } else {
        const auto selected = PickExecutable();
        if (!selected.has_value()) {
            std::cout << "No executable was selected.\n";
            return 1;
        }
        gameExecutable = *selected;
    }

    return RunLauncher(gameExecutable, gameArguments);
}