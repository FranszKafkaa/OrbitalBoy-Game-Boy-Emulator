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

RaSecretString::RaSecretString(SecureStringWipeObserver observer)
    : observer_(std::move(observer)) {}

RaSecretString::~RaSecretString() {
    clear();
}

RaSecretString::RaSecretString(RaSecretString&& other)
    : observer_(other.observer_) {
    if (!other.storage_.empty()) {
        storage_.assign(other.storage_.data(), other.storage_.size());
    }
    other.clear();
}

RaSecretString& RaSecretString::operator=(RaSecretString&& other) {
    if (this != &other) {
        clear();
        observer_ = other.observer_;
        if (!other.storage_.empty()) {
            storage_.assign(other.storage_.data(), other.storage_.size());
        }
        other.clear();
    }
    return *this;
}

void RaSecretString::assign(std::string_view value) {
    clear();
    if (!value.empty()) {
        storage_.assign(value.data(), value.size());
    }
}

void RaSecretString::assignAndErase(std::string& source) {
    assign(std::string_view(source.data(), source.size()));
    (void)secureEraseStringStorage(source, observer_);
}

void RaSecretString::clear(const SecureStringWipeObserver& observer) {
    (void)secureEraseStringStorage(
        storage_,
        observer ? observer : observer_
    );
}

bool RaSecretString::empty() const {
    return storage_.empty();
}

std::size_t RaSecretString::size() const {
    return storage_.size();
}

const char* RaSecretString::c_str() const {
    return storage_.c_str();
}

std::string_view RaSecretString::view() const {
    return storage_;
}

} // namespace gb::frontend
