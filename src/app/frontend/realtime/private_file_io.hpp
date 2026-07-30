#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace gb::frontend::detail {

bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size
);

bool makeFileOwnerPrivate(const std::filesystem::path& path);

inline bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    std::string_view contents
) {
    return writePrivateFileAtomically(
        destination,
        reinterpret_cast<const std::uint8_t*>(contents.data()),
        contents.size()
    );
}

inline bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::vector<std::uint8_t>& contents
) {
    return writePrivateFileAtomically(
        destination,
        contents.data(),
        contents.size()
    );
}

} // namespace gb::frontend::detail
