#include "gb/app/frontend/realtime/replay_io.hpp"

#include <cstdint>
#include <fstream>

namespace gb::frontend {

namespace {

constexpr std::uint32_t ReplayMagic = 0x31505247; // GRP1

template <typename T>
bool writePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
bool readPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

} // namespace

bool saveReplayFile(const std::string& path, const ReplayData& replay) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(replay.frameInputs.size());
    if (!writePod(out, ReplayMagic)
        || !writePod(out, replay.version)
        || !writePod(out, replay.seed)
        || !writePod(out, count)) {
        return false;
    }

    if (count > 0) {
        out.write(reinterpret_cast<const char*>(replay.frameInputs.data()), static_cast<std::streamsize>(count));
    }
    return static_cast<bool>(out);
}

std::optional<ReplayData> loadReplayFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    std::uint32_t magic = 0;
    ReplayData replay{};
    std::uint32_t count = 0;
    if (!readPod(in, magic)
        || !readPod(in, replay.version)
        || !readPod(in, replay.seed)
        || !readPod(in, count)) {
        return std::nullopt;
    }
    if (magic != ReplayMagic || replay.version == 0 || count > (10u * 60u * 60u * 60u)) {
        return std::nullopt;
    }

    replay.frameInputs.assign(count, 0);
    if (count > 0) {
        in.read(reinterpret_cast<char*>(replay.frameInputs.data()), static_cast<std::streamsize>(count));
        if (!in) {
            return std::nullopt;
        }
    }
    return replay;
}

std::uint8_t packButtons(
    bool right,
    bool left,
    bool up,
    bool down,
    bool a,
    bool b,
    bool select,
    bool start
) {
    std::uint8_t mask = 0;
    if (right) mask |= 1u << 0;
    if (left) mask |= 1u << 1;
    if (up) mask |= 1u << 2;
    if (down) mask |= 1u << 3;
    if (a) mask |= 1u << 4;
    if (b) mask |= 1u << 5;
    if (select) mask |= 1u << 6;
    if (start) mask |= 1u << 7;
    return mask;
}

} // namespace gb::frontend
