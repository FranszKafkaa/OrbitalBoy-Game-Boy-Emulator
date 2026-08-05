#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "gb/achievements/storage/private_file_io.hpp"

#include "../achievement_test_utils.hpp"
#include "../../test_framework.hpp"

namespace {
using achievement_tests::ScopedPath;
using achievement_tests::readBytes;
using achievement_tests::readText;
using achievement_tests::temporaryPath;
using achievement_tests::writeBytes;
using achievement_tests::writeText;

} // namespace

TEST_CASE("achievement_storage", "canonical_atomic_write_preserves_sibling_temporary_file") {
    const auto directory = temporaryPath("achievement_storage_atomic");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto destination = directory / "config";
    const auto sibling = directory / "config.tmp";
    writeText(sibling, "do-not-replace");

    T_REQUIRE(gb::achievements::storage::writePrivateFileAtomically(
        destination,
        std::string_view("canonical-data")
    ));
    T_EQ(readText(destination), std::string("canonical-data"));
    T_EQ(readText(sibling), std::string("do-not-replace"));
}

TEST_CASE("achievement_storage", "canonical_atomic_write_cleans_failed_temporary_file") {
    const auto directory = temporaryPath("achievement_storage_cleanup");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto destination = directory / "config";
    const auto temporary = directory / "config.temporary";
    std::vector<gb::achievements::storage::PrivateFileIoEvent> events;
    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.chooseTemporaryPath = [&](const auto&, int) { return temporary; };
    hooks.allowReplace = [](const auto&, const auto&) { return false; };
    hooks.syncDirectory = [](const auto&) { return true; };
    hooks.trace = [&](const auto event, const auto&) { events.push_back(event); };

    T_REQUIRE(!gb::achievements::storage::writePrivateFileAtomically(
        destination,
        std::string_view("data"),
        &hooks
    ));
    T_REQUIRE(!std::filesystem::exists(temporary));
    T_REQUIRE(events == std::vector<gb::achievements::storage::PrivateFileIoEvent>({
        gb::achievements::storage::PrivateFileIoEvent::TemporaryCreated,
        gb::achievements::storage::PrivateFileIoEvent::TemporarySynced,
        gb::achievements::storage::PrivateFileIoEvent::TemporaryRemoved,
        gb::achievements::storage::PrivateFileIoEvent::DirectorySynced,
    }));
}

TEST_CASE("achievement_storage", "canonical_durable_rename_and_remove_change_real_entries") {
    const auto directory = temporaryPath("achievement_storage_entries");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto source = directory / "source";
    const auto destination = directory / "destination";
    writeText(source, "data");
    bool changed = false;

    T_REQUIRE(gb::achievements::storage::renameFileDurably(
        source,
        destination,
        &changed
    ));
    T_REQUIRE(changed);
    T_REQUIRE(!std::filesystem::exists(source));
    T_EQ(readText(destination), std::string("data"));

    T_REQUIRE(gb::achievements::storage::removeFileDurably(
        destination,
        &changed
    ));
    T_REQUIRE(changed);
    T_REQUIRE(!std::filesystem::exists(destination));
}

TEST_CASE("achievement_storage", "canonical_atomic_write_reports_directory_sync_failure_after_replacement") {
    const auto directory = temporaryPath("achievement_storage_sync_failure");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto destination = directory / "config";
    std::vector<gb::achievements::storage::PrivateFileIoEvent> events;
    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.trace = [&](const auto event, const auto&) { events.push_back(event); };
    hooks.syncDirectory = [](const auto&) { return false; };

    T_REQUIRE(!gb::achievements::storage::writePrivateFileAtomically(
        destination,
        std::string_view("written-but-not-durable"),
        &hooks
    ));
    T_EQ(readText(destination), std::string("written-but-not-durable"));
    T_REQUIRE(events.size() >= 3U);
    T_REQUIRE(events[events.size() - 2U]
        == gb::achievements::storage::PrivateFileIoEvent::Replaced);
    T_REQUIRE(events.back()
        == gb::achievements::storage::PrivateFileIoEvent::DirectorySyncFailed);
}

TEST_CASE("achievement_storage", "canonical_durable_rename_never_overwrites_existing_entry") {
    const auto directory = temporaryPath("achievement_storage_rename_exclusive");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto source = directory / "source";
    const auto destination = directory / "destination";
    writeBytes(source, {1});
    writeBytes(destination, {2});
    bool changed = true;

    T_REQUIRE(!gb::achievements::storage::renameFileDurably(
        source,
        destination,
        &changed
    ));
    T_REQUIRE(!changed);
    T_REQUIRE(readBytes(source) == std::vector<std::uint8_t>({1}));
    T_REQUIRE(readBytes(destination) == std::vector<std::uint8_t>({2}));
}

#if !defined(_WIN32)
TEST_CASE("achievement_storage", "canonical_atomic_failure_cleans_temporary_syncs_directory_and_preserves_errno") {
    const auto directory = temporaryPath("achievement_storage_errno");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto destination = directory / "config";
    const auto temporary = directory / "chosen-temporary";
    std::vector<gb::achievements::storage::PrivateFileIoEvent> events;
    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.chooseTemporaryPath = [&](const auto&, int) { return temporary; };
    hooks.allowReplace = [](const auto&, const auto&) {
        errno = EIO;
        return false;
    };
    hooks.trace = [&](const auto event, const auto&) { events.push_back(event); };
    hooks.syncDirectory = [](const auto&) { return true; };

    errno = 0;
    T_REQUIRE(!gb::achievements::storage::writePrivateFileAtomically(
        destination,
        std::string_view("secret"),
        &hooks
    ));
    T_EQ(errno, EIO);
    T_REQUIRE(!std::filesystem::exists(destination));
    T_REQUIRE(!std::filesystem::exists(temporary));
    T_REQUIRE(events == std::vector<gb::achievements::storage::PrivateFileIoEvent>({
        gb::achievements::storage::PrivateFileIoEvent::TemporaryCreated,
        gb::achievements::storage::PrivateFileIoEvent::TemporarySynced,
        gb::achievements::storage::PrivateFileIoEvent::TemporaryRemoved,
        gb::achievements::storage::PrivateFileIoEvent::DirectorySynced,
    }));
}

TEST_CASE("achievement_storage", "canonical_private_file_operations_do_not_follow_symlinks") {
    const auto directory = temporaryPath("achievement_storage_symlink");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto outside = directory / "outside";
    const auto collision = directory / "temporary-collision";
    const auto destination = directory / "config";
    writeText(outside, "keep");
    T_REQUIRE(::chmod(
        outside.c_str(),
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
    ) == 0);
    std::filesystem::create_symlink(outside, collision);

    gb::achievements::storage::PrivateFileIoHooks hooks{};
    hooks.chooseTemporaryPath = [&](const auto&, int attempt) {
        return attempt == 0 ? collision : directory / "temporary-success";
    };
    hooks.syncDirectory = [](const auto&) { return true; };
    T_REQUIRE(gb::achievements::storage::writePrivateFileAtomically(
        destination,
        std::string_view("safe"),
        &hooks
    ));
    T_REQUIRE(std::filesystem::is_symlink(collision));
    T_EQ(readText(outside), std::string("keep"));

    const auto privateLink = directory / "private-link";
    std::filesystem::create_symlink(outside, privateLink);
    T_REQUIRE(!gb::achievements::storage::makeFileOwnerPrivate(privateLink));
    struct stat status {};
    T_REQUIRE(::stat(outside.c_str(), &status) == 0);
    T_EQ(
        status.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO),
        static_cast<mode_t>(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
    );
}

TEST_CASE("achievement_storage", "canonical_private_file_permissions_are_owner_only") {
    const auto directory = temporaryPath("achievement_storage_permissions");
    ScopedPath cleanup(directory);
    T_REQUIRE(std::filesystem::create_directories(directory));
    const auto path = directory / "config";
    writeText(path, "secret");
    T_REQUIRE(::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == 0);

    T_REQUIRE(gb::achievements::storage::makeFileOwnerPrivate(path));
    struct stat status {};
    T_REQUIRE(::stat(path.c_str(), &status) == 0);
    T_EQ(
        status.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO),
        static_cast<mode_t>(S_IRUSR | S_IWUSR)
    );
}
#endif
