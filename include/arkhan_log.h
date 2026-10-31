#pragma once

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>

namespace arkhan {

class Log final {
public:
    static Log& Instance();

    void Initialize(const std::filesystem::path& root);
    void Write(const char* scope, const std::string& message);
    std::string Snapshot() const;
    std::filesystem::path Root() const;

private:
    mutable std::mutex mutex_;
    std::filesystem::path root_;
    std::filesystem::path file_;
    std::deque<std::string> lines_;
};

} // namespace arkhan
