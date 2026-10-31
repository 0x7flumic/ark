#include "arkhan_log.h"

#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace arkhan {

Log& Log::Instance() {
    static Log instance;
    return instance;
}

void Log::Initialize(const std::filesystem::path& root) {
    std::lock_guard lock(mutex_);
    root_ = root;
    file_ = root_ / L"logs" / L"Arkhan.native.log";
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
}

void Log::Write(const char* scope, const std::string& message) {
    SYSTEMTIME local{};
    GetLocalTime(&local);

    std::ostringstream stream;
    stream << '[' << std::setfill('0') << std::setw(2) << local.wHour << ':'
           << std::setw(2) << local.wMinute << ':' << std::setw(2) << local.wSecond << '.'
           << std::setw(3) << local.wMilliseconds << "] [" << scope << "] " << message;
    const auto line = stream.str();

    std::lock_guard lock(mutex_);
    lines_.push_back(line);
    while (lines_.size() > 1200) lines_.pop_front();

    if (!file_.empty()) {
        std::ofstream output(file_, std::ios::binary | std::ios::app);
        if (output) output << line << "\r\n";
    }
}

std::string Log::Snapshot() const {
    std::lock_guard lock(mutex_);
    std::ostringstream stream;
    for (const auto& line : lines_) stream << line << '\n';
    return stream.str();
}

std::filesystem::path Log::Root() const {
    std::lock_guard lock(mutex_);
    return root_;
}

} // namespace arkhan
