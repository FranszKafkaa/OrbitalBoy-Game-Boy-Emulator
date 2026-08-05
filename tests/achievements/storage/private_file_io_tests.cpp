#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gb/achievements/storage/private_file_io.hpp"

#include "../../test_framework.hpp"

namespace {

std::filesystem::path temporaryDirectory(const std::string& name) {
    static std::uint64_t counter = 0;
    const auto now = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / (name + "_" + std::to_string(now) + "_"
           + std::to_string(++counter));
}

class ScopedPath {
public:
    explicit ScopedPath(std::filesystem::path path)
        : path_(std::move(path)) {}

    ~ScopedPath() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void writeText(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

} // namespace

TEST_CASE("achievement_storage", "canonical_atomic_write_preserves_sibling_temporary_file") {
    const auto directory = temporaryDirectory("achievement_storage_atomic");
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
    const auto directory = temporaryDirectory("achievement_storage_cleanup");
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
    const auto directory = temporaryDirectory("achievement_storage_entries");
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
