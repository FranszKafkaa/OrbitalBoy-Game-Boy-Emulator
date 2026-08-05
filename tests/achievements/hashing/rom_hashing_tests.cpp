#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "gb/achievements/hashing/md5.hpp"
#include "gb/achievements/hashing/rom_hasher.hpp"

#include "../achievement_test_utils.hpp"
#include "../../test_framework.hpp"

namespace {

using gb::achievements::hashing::Md5;
using gb::achievements::hashing::RomSystem;
using achievement_tests::ScopedPath;
using achievement_tests::temporaryPath;
using achievement_tests::writeBytes;

std::string digestInPieces(std::string_view message) {
    Md5 md5;
    md5.update(nullptr, 0U);
    for (std::size_t begin = 0U; begin < message.size();) {
        const auto count = std::min<std::size_t>(3U, message.size() - begin);
        md5.update(
            reinterpret_cast<const std::uint8_t*>(message.data() + begin),
            count
        );
        begin += count;
    }
    return md5.digest().hexLowercase();
}

} // namespace

TEST_CASE("rom_hashing", "md5_matches_all_rfc_1321_test_vectors") {
    struct Vector {
        const char* message;
        const char* expected;
    };
    const std::array<Vector, 7> vectors{{
        {"", "d41d8cd98f00b204e9800998ecf8427e"},
        {"a", "0cc175b9c0f1b6a831c399e269772661"},
        {"abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
        {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
         "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    }};

    for (const auto& vector : vectors) {
        T_EQ(
            gb::achievements::hashing::md5(std::string_view(vector.message)).hexLowercase(),
            std::string(vector.expected)
        );
    }
}

TEST_CASE("rom_hashing", "incremental_md5_handles_padding_boundaries_and_multiblock_messages") {
    struct Boundary {
        std::size_t count;
        const char* expected;
    };
    const std::array<Boundary, 6> boundaries{{
        {55U, "ef1772b6dff9a122358552954ad0df65"},
        {56U, "3b0c8ac703f828b04c6c197006d17218"},
        {63U, "b06521f39153d618550606be297466d5"},
        {64U, "014842d480b571495a4a0363793f7367"},
        {65U, "c743a45e0d2e6a95cb859adae0248435"},
        {257U, "b7958df91b9413477491e9b6e27f1bac"},
    }};

    for (const auto& boundary : boundaries) {
        const std::string message(boundary.count, 'a');
        T_EQ(digestInPieces(message), std::string(boundary.expected));
    }
}

TEST_CASE("rom_hashing", "incremental_md5_accepts_zero_length_updates_between_data") {
    Md5 md5;
    const std::string first = "a";
    const std::string second = "bc";
    md5.update(
        reinterpret_cast<const std::uint8_t*>(first.data()),
        first.size()
    );
    md5.update(nullptr, 0U);
    md5.update(
        reinterpret_cast<const std::uint8_t*>(second.data()),
        second.size()
    );
    md5.update(nullptr, 0U);

    T_EQ(md5.digest().hexLowercase(), std::string("900150983cd24fb0d6963f7d28e17f72"));
}

TEST_CASE("rom_hashing", "incremental_md5_tracks_lengths_beyond_32_bit_bit_counts") {
    const std::vector<std::uint8_t> block(1024U * 1024U, static_cast<std::uint8_t>('a'));
    Md5 md5;
    for (std::size_t iteration = 0U; iteration < 513U; ++iteration) {
        md5.update(block.data(), block.size());
    }

    T_EQ(md5.digest().hexLowercase(), std::string("f0abebcac19eb4914e7cb04084dd54a6"));
}

TEST_CASE("rom_hashing", "md5_formats_a_lowercase_32_character_digest") {
    const auto digest = gb::achievements::hashing::md5("The quick brown fox jumps over the lazy dog");
    T_EQ(digest.hexLowercase(), std::string("9e107d9d372bb6826bd81d3542a419d6"));
    T_EQ(digest.hexLowercase().size(), 32U);
}

TEST_CASE("rom_hashing", "md5_one_shot_byte_helper_hashes_binary_input") {
    const std::vector<std::uint8_t> binary{'a', 'b', 'c', 0U, 255U};
    T_EQ(
        gb::achievements::hashing::md5(binary).hexLowercase(),
        std::string("0b44e1d31c72c175e7a4380ae5fe5e6a")
    );
}

TEST_CASE("rom_hashing", "every_explicit_rom_system_hashes_complete_synthetic_rom_bytes") {
    const std::vector<std::uint8_t> rom{
        'O', 'r', 'b', 'i', 't', 'a', 'l', 'B', 'o', 'y', ' ', 'R', 'O', 'M',
        ' ', 'i', 'd', 'e', 'n', 't', 'i', 't', 'y', 0U, 1U, 255U
    };
    const std::array<RomSystem, 3> systems{{
        RomSystem::GameBoy,
        RomSystem::GameBoyColor,
        RomSystem::GameBoyAdvance,
    }};

    for (const auto system : systems) {
        const auto result = gb::achievements::hashing::hashRomBytes(system, rom);
        T_EQ(result.hash, std::string("542ba454d5195a2602ba50eb64eb9f6c"));
        T_EQ(result.bytesHashed, 26U);
    }
}

TEST_CASE("rom_hashing", "streamed_rom_file_hash_matches_known_digest_and_reports_exact_bytes") {
    const auto path = temporaryPath("rom_hash_streamed");
    ScopedPath cleanup(path);
    const std::vector<std::uint8_t> rom{
        's', 't', 'r', 'e', 'a', 'm', 'e', 'd', ' ', 'R', 'O', 'M', ' ',
        'f', 'i', 'x', 't', 'u', 'r', 'e', 0U, 'd', 'a', 't', 'a'
    };
    writeBytes(path, rom);

    const auto result = gb::achievements::hashing::hashRomFile(RomSystem::GameBoyColor, path);
    T_REQUIRE(result.has_value());
    T_EQ(result->hash, std::string("d71f27dc2735565285e3dd77d516c627"));
    T_EQ(result->bytesHashed, 25U);
}

TEST_CASE("rom_hashing", "streamed_rom_file_hashes_empty_files_without_ambiguity") {
    const auto path = temporaryPath("rom_hash_empty");
    ScopedPath cleanup(path);
    writeBytes(path, {});

    const auto result = gb::achievements::hashing::hashRomFile(RomSystem::GameBoy, path);
    T_REQUIRE(result.has_value());
    T_EQ(result->hash, std::string("d41d8cd98f00b204e9800998ecf8427e"));
    T_EQ(result->bytesHashed, 0U);
}

TEST_CASE("rom_hashing", "streamed_rom_file_hash_rejects_missing_paths_and_directories") {
    const auto missing = temporaryPath("rom_hash_missing");
    T_REQUIRE(!gb::achievements::hashing::hashRomFile(RomSystem::GameBoyAdvance, missing).has_value());

    const auto directory = temporaryPath("rom_hash_directory");
    ScopedPath cleanup(directory);
    std::filesystem::create_directory(directory);
    T_REQUIRE(!gb::achievements::hashing::hashRomFile(RomSystem::GameBoyAdvance, directory).has_value());
}
