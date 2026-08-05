#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace gb::achievements {

using SecretWipeObserver =
    std::function<void(const char* bytes, std::size_t storageSize)>;

bool secureEraseStringStorage(
    std::string& value,
    const SecretWipeObserver& observer = {},
    std::size_t maximumStorageBytes = 1024U * 1024U
);

class SecretString {
public:
    explicit SecretString(SecretWipeObserver observer = {});
    ~SecretString();

    SecretString(const SecretString&) = delete;
    SecretString& operator=(const SecretString&) = delete;
    SecretString(SecretString&& other);
    SecretString& operator=(SecretString&& other);

    void assign(std::string_view value);
    void assignAndErase(std::string& source);
    void clear(const SecretWipeObserver& observer = {});

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] const char* c_str() const;
    [[nodiscard]] std::string_view view() const;

private:
    std::string storage_{};
    SecretWipeObserver observer_{};
};

} // namespace gb::achievements
