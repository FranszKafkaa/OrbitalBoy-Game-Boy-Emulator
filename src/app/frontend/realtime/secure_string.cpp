#include "gb/app/frontend/realtime/secure_string.hpp"

#include <algorithm>
#include <utility>

namespace gb::frontend {

bool secureEraseStringStorage(
    std::string& value,
    const SecureStringWipeObserver& observer,
    std::size_t maximumStorageBytes
) {
    const std::size_t storageSize = value.capacity();
    const bool fullStorageCanBeWiped =
        storageSize <= maximumStorageBytes;
    std::size_t wipeSize = value.size();
    if (fullStorageCanBeWiped) {
        try {
            value.resize(storageSize, '\0');
            wipeSize = storageSize;
        } catch (...) {
            wipeSize = value.size();
        }
    }
    volatile char* bytes = value.empty()
        ? nullptr
        : reinterpret_cast<volatile char*>(value.data());
    for (std::size_t index = 0; index < wipeSize; ++index) {
        bytes[index] = '\0';
    }
    if (observer && wipeSize != 0U) {
        observer(value.data(), wipeSize);
    }
    std::string empty;
    value.swap(empty);
    return fullStorageCanBeWiped && wipeSize == storageSize;
}

std::string moveStringAndEraseSource(
    std::string& source,
    const SecureStringWipeObserver& observer
) {
    std::string destination(std::move(source));
    (void)secureEraseStringStorage(source, observer);
    return destination;
}

} // namespace gb::frontend
