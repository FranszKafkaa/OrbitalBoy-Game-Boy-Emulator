#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace achievement_tests {

inline std::filesystem::path temporaryPath(const std::string& name) {
    static std::uint64_t counter = 0;
    const auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / (name + "_" + std::to_string(now) + "_"
           + std::to_string(++counter));
}

class ScopedPath {
public:
    explicit ScopedPath(std::filesystem::path path)
        : path_(std::move(path)) {}

    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

    ~ScopedPath() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

inline std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

inline void writeText(
    const std::filesystem::path& path,
    std::string_view contents
) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

inline std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

inline void writeBytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& contents
) {
    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(contents.data()),
        static_cast<std::streamsize>(contents.size())
    );
}

} // namespace achievement_tests
