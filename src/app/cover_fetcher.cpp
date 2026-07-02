#include "gb/app/cover_fetcher.hpp"

#include "gb/app/runtime_paths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gb {

namespace {

bool hasRomExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".gb" || ext == ".gbc" || ext == ".gba";
}

bool hasImageExtension(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}

std::string toLowerAscii(std::string text);

bool hasPrefixBytes(const std::filesystem::path& path, const std::vector<unsigned char>& expected) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::vector<unsigned char> bytes(expected.size(), 0U);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return in.gcount() == static_cast<std::streamsize>(bytes.size()) && bytes == expected;
}

bool isValidImageFile(const std::filesystem::path& path) {
    if (hasPrefixBytes(path, {0x89U, 'P', 'N', 'G', 0x0DU, 0x0AU, 0x1AU, 0x0AU})
        || hasPrefixBytes(path, {0xFFU, 0xD8U, 0xFFU})
        || hasPrefixBytes(path, {'B', 'M'})) {
        return true;
    }
    const std::string ext = toLowerAscii(path.extension().string());
    if (ext == ".png") {
        return false;
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return false;
    }
    if (ext == ".bmp") {
        return false;
    }
    return false;
}

std::optional<std::string> readThumbnailAlias(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path, ec) > 512U) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    if (text.size() <= 4U || toLowerAscii(text.substr(text.size() - 4U)) != ".png") {
        return std::nullopt;
    }
    return text.substr(0, text.size() - 4U);
}

std::string removeRevisionTag(std::string text) {
    const std::array<std::string, 4> markers{" (Rev ", " (Revision ", " (Beta ", " (Proto "};
    for (const auto& marker : markers) {
        const auto pos = text.find(marker);
        if (pos != std::string::npos) {
            return text.substr(0, pos);
        }
    }
    return text;
}

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::optional<std::string> libretroThumbnailRepoForRom(const std::filesystem::path& romPath) {
    const std::string ext = toLowerAscii(romPath.extension().string());
    if (ext == ".gb") {
        return "Nintendo_-_Game_Boy";
    }
    if (ext == ".gbc") {
        return "Nintendo_-_Game_Boy_Color";
    }
    if (ext == ".gba") {
        return "Nintendo_-_Game_Boy_Advance";
    }
    return std::nullopt;
}

bool isUnreservedUrlChar(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9')
        || ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string urlEncode(const std::string& text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char ch : text) {
        if (isUnreservedUrlChar(ch)) {
            out.push_back(static_cast<char>(ch));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[(ch >> 4U) & 0x0FU]);
        out.push_back(kHex[ch & 0x0FU]);
    }
    return out;
}

std::string shellQuote(const std::string& text) {
#if defined(_WIN32)
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out.push_back(ch);
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (const char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out += "'";
    return out;
#endif
}

std::string coverUrl(const std::string& repo, const std::string& gameName) {
    return "https://raw.githubusercontent.com/libretro-thumbnails/"
        + repo
        + "/master/Named_Boxarts/"
        + urlEncode(gameName + ".png");
}

bool downloadUrlToFile(const std::string& url, const std::filesystem::path& outputPath) {
    const std::string command = std::string("curl -L --fail --silent --show-error --output ")
        + shellQuote(outputPath.string())
        + " "
        + shellQuote(url);
    const bool ok = std::system(command.c_str()) == 0;
    if (!ok) {
        std::cout << "falha url: " << url << "\n";
    }
    return ok;
}

std::vector<std::string> candidateNamesForRom(const std::filesystem::path& romPath) {
    std::vector<std::string> names;
    const auto pushUnique = [&](std::string value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        if (!value.empty() && std::find(names.begin(), names.end(), value) == names.end()) {
            names.push_back(std::move(value));
        }
    };

    pushUnique(romPath.stem().string());
    pushUnique(removeRevisionTag(romPath.stem().string()));
    if (romPath.has_parent_path()) {
        pushUnique(romPath.parent_path().filename().string());
    }
    return names;
}

bool directoryHasUsableImage(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && hasImageExtension(entry.path()) && isValidImageFile(entry.path())) {
            return true;
        }
    }
    return false;
}

std::vector<std::filesystem::path> discoverRomFiles() {
    std::vector<std::filesystem::path> roms;
    std::set<std::string> seen;
    for (const auto& dirText : gb::romSearchDirectoriesForRuntime()) {
        const std::filesystem::path dir(dirText);
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file() || !hasRomExtension(entry.path())) {
                continue;
            }
            const auto resolved = std::filesystem::absolute(entry.path()).lexically_normal();
            if (seen.insert(resolved.string()).second) {
                roms.push_back(resolved);
            }
        }
    }
    std::sort(roms.begin(), roms.end());
    return roms;
}

} // namespace

int fetchMissingRomCovers(bool force) {
    const auto roms = discoverRomFiles();
    if (roms.empty()) {
        std::cout << "nenhuma ROM encontrada em ./rom ou ./roms\n";
        return 0;
    }

    int downloaded = 0;
    int skipped = 0;
    int failed = 0;

    for (const auto& romPath : roms) {
        const auto repo = libretroThumbnailRepoForRom(romPath);
        if (!repo.has_value()) {
            ++skipped;
            continue;
        }

        const auto romDir = romPath.parent_path();
        const auto coverPath = romDir / "capa.png";
        if (!force && directoryHasUsableImage(romDir)) {
            ++skipped;
            std::cout << "capa ja existe: " << romDir << "\n";
            continue;
        }

        bool ok = false;
        const auto tmpPath = romDir / ".capa.download";
        for (const auto& name : candidateNamesForRom(romPath)) {
            const auto url = coverUrl(*repo, name);
            std::cout << "buscando capa: " << name << "\n";
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            if (!downloadUrlToFile(url, tmpPath)) {
                continue;
            }
            if (!isValidImageFile(tmpPath)) {
                if (const auto alias = readThumbnailAlias(tmpPath)) {
                    std::filesystem::remove(tmpPath, ec);
                    const auto aliasUrl = coverUrl(*repo, *alias);
                    std::cout << "seguindo alias: " << *alias << "\n";
                    if (!downloadUrlToFile(aliasUrl, tmpPath)) {
                        continue;
                    }
                } else {
                    std::filesystem::remove(tmpPath, ec);
                    continue;
                }
            }
            if (!std::filesystem::exists(tmpPath) || std::filesystem::file_size(tmpPath, ec) == 0U || !isValidImageFile(tmpPath)) {
                std::cout << "download nao gerou PNG valido\n";
                std::filesystem::remove(tmpPath, ec);
                continue;
            }
            std::filesystem::copy_file(tmpPath, coverPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                std::filesystem::remove(tmpPath, ec);
                std::cout << "capa salva: " << coverPath << "\n";
                ok = true;
                ++downloaded;
                break;
            } else {
                std::cout << "falha ao salvar capa: " << ec.message() << "\n";
            }
        }

        if (!ok) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            ++failed;
            std::cout << "capa nao encontrada: " << romPath.filename().string() << "\n";
        }
    }

    std::cout << "capas: baixadas=" << downloaded
              << " ignoradas=" << skipped
              << " falhas=" << failed
              << "\n";
    return failed == 0 ? 0 : 2;
}

} // namespace gb
