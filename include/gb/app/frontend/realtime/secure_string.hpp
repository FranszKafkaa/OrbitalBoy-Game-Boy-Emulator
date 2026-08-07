#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace gb::frontend {

using SecureStringWipeObserver =
    std::function<void(const char* bytes, std::size_t storageSize)>;

bool secureEraseStringStorage(
    std::string& value,
    const SecureStringWipeObserver& observer = {},
    std::size_t maximumStorageBytes = 1024U * 1024U
);

class RaSecretString {
public:
    explicit RaSecretString(SecureStringWipeObserver observer = {});
    ~RaSecretString();

    RaSecretString(const RaSecretString&) = delete;
    RaSecretString& operator=(const RaSecretString&) = delete;
    RaSecretString(RaSecretString&& other);
    RaSecretString& operator=(RaSecretString&& other);

    void assign(std::string_view value);
    void assignAndErase(std::string& source);
    void clear(const SecureStringWipeObserver& observer = {});

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const char* c_str() const;
    [[nodiscard]] std::string_view view() const;

private:
    std::string storage_{};
    SecureStringWipeObserver observer_{};
};

} // namespace gb::frontend
