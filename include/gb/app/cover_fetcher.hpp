#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gb {

std::vector<std::string> coverCandidateNamesForRom(const std::filesystem::path& romPath);
bool downloadCoverForRomName(
    const std::filesystem::path& romPath,
    const std::string& gameName,
    const std::filesystem::path& outputPath
);
int fetchMissingRomCovers(bool force);

} // namespace gb
