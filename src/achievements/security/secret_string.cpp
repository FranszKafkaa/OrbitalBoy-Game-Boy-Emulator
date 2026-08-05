#include "gb/achievements/security/secret_string.hpp"

#include <utility>

namespace gb::achievements {

bool secureEraseStringStorage(
    std::string& value,
    const SecretWipeObserver& observer,
    std::size_t maximumStorageBytes
) {
    const std::size_t storageSize = value.capacity();
    const bool fullStorageCanBeWiped = storageSize <= maximumStorageBytes;
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

SecretString::SecretString(SecretWipeObserver observer)
    : observer_(std::move(observer)) {}

SecretString::~SecretString() {
    clear();
}

SecretString::SecretString(SecretString&& other)
    : observer_(other.observer_) {
    if (!other.storage_.empty()) {
        storage_.assign(other.storage_.data(), other.storage_.size());
    }
    other.clear();
}

SecretString& SecretString::operator=(SecretString&& other) {
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

void SecretString::assign(std::string_view value) {
    clear();
    if (!value.empty()) {
        storage_.assign(value.data(), value.size());
    }
}

void SecretString::assignAndErase(std::string& source) {
    assign(std::string_view(source.data(), source.size()));
    (void)secureEraseStringStorage(source, observer_);
}

void SecretString::clear(const SecretWipeObserver& observer) {
    (void)secureEraseStringStorage(
        storage_,
        observer ? observer : observer_
    );
}

bool SecretString::empty() const {
    return storage_.empty();
}

std::size_t SecretString::size() const {
    return storage_.size();
}

const char* SecretString::c_str() const {
    return storage_.c_str();
}

std::string_view SecretString::view() const {
    return storage_;
}

} // namespace gb::achievements
