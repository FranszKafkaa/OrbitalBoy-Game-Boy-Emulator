#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace gb::frontend {

using SecureStringWipeObserver =
    std::function<void(const char* bytes, std::size_t storageSize)>;

bool secureEraseStringStorage(
    std::string& value,
    const SecureStringWipeObserver& observer = {},
    std::size_t maximumStorageBytes = 1024U * 1024U
);

std::string moveStringAndEraseSource(
    std::string& source,
    const SecureStringWipeObserver& observer = {}
);

} // namespace gb::frontend
