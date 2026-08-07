#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace gb::achievements::storage {

enum class PrivateFileIoEvent {
    TemporaryCreated,
    TemporarySynced,
    TemporaryRemoved,
    TemporaryRemoveFailed,
    Replaced,
    Removed,
    Renamed,
    DirectorySynced,
    DirectorySyncFailed,
};

struct PrivateFileIoHooks {
    std::function<std::filesystem::path(
        const std::filesystem::path& destination,
        int attempt
    )> chooseTemporaryPath;
    std::function<void(
        PrivateFileIoEvent event,
        const std::filesystem::path& path
    )> trace;
    std::function<bool(
        const std::filesystem::path& temporary,
        const std::filesystem::path& destination
    )> allowReplace;
    std::function<bool(const std::filesystem::path& directory)> syncDirectory;
};

bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size,
    const PrivateFileIoHooks* hooks = nullptr
);

bool removeFileDurably(
    const std::filesystem::path& path,
    bool* entryChanged = nullptr,
    const PrivateFileIoHooks* hooks = nullptr
);

bool renameFileDurably(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    bool* entryChanged = nullptr,
    const PrivateFileIoHooks* hooks = nullptr
);

bool makeFileOwnerPrivate(const std::filesystem::path& path);

inline bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    std::string_view contents,
    const PrivateFileIoHooks* hooks = nullptr
) {
    return writePrivateFileAtomically(
        destination,
        reinterpret_cast<const std::uint8_t*>(contents.data()),
        contents.size(),
        hooks
    );
}

inline bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::vector<std::uint8_t>& contents,
    const PrivateFileIoHooks* hooks = nullptr
) {
    return writePrivateFileAtomically(
        destination,
        contents.data(),
        contents.size(),
        hooks
    );
}

} // namespace gb::achievements::storage
