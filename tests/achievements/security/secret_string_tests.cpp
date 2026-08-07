#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "gb/achievements/security/secret_string.hpp"

#include "../../test_framework.hpp"

TEST_CASE("achievements_security", "canonical_secret_string_moves_and_wipes_owned_storage") {
    std::string source = "owned-secret";
    const std::size_t sourceCapacity = source.capacity();
    std::vector<std::size_t> wipedSizes;
    bool allZeroes = true;
    const gb::achievements::SecretWipeObserver observer =
        [&](const char* bytes, std::size_t size) {
            wipedSizes.push_back(size);
            allZeroes = allZeroes && bytes != nullptr
                && std::all_of(bytes, bytes + size, [](char byte) {
                    return byte == '\0';
                });
        };

    {
        gb::achievements::SecretString first(observer);
        first.assignAndErase(source);
        T_REQUIRE(source.empty());
        T_EQ(first.view(), std::string_view("owned-secret"));

        gb::achievements::SecretString second(std::move(first));
        T_REQUIRE(first.empty());
        T_EQ(second.view(), std::string_view("owned-secret"));
        second.clear();
        T_REQUIRE(second.empty());
    }

    T_REQUIRE(!wipedSizes.empty());
    T_REQUIRE(std::all_of(
        wipedSizes.begin(),
        wipedSizes.end(),
        [&](std::size_t size) { return size >= sourceCapacity; }
    ));
    T_REQUIRE(allZeroes);
}

TEST_CASE("achievements_security", "canonical_eraser_observes_and_releases_short_storage") {
    std::string value = "wipe-me";
    const std::size_t allocated = value.capacity();
    std::size_t observedSize = 0U;
    bool observedZeroes = false;

    T_REQUIRE(gb::achievements::secureEraseStringStorage(
        value,
        [&](const char* bytes, std::size_t size) {
            observedSize = size;
            observedZeroes = bytes != nullptr
                && std::all_of(bytes, bytes + size, [](char byte) {
                    return byte == '\0';
                });
        }
    ));

    T_EQ(observedSize, allocated);
    T_REQUIRE(observedZeroes);
    T_REQUIRE(value.empty());
}
