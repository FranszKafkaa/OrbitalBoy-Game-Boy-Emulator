#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/frontend/realtime/retroachievements_http.hpp"

namespace gb::frontend {

[[nodiscard]] std::string cacheKey(std::string_view url);

class RetroAchievementsImageCache {
public:
    explicit RetroAchievementsImageCache(RaHttpTransport& transport, std::string cacheDirectory = {});
    ~RetroAchievementsImageCache();

    RetroAchievementsImageCache(const RetroAchievementsImageCache&) = delete;
    RetroAchievementsImageCache& operator=(const RetroAchievementsImageCache&) = delete;
    RetroAchievementsImageCache(RetroAchievementsImageCache&&) = delete;
    RetroAchievementsImageCache& operator=(RetroAchievementsImageCache&&) = delete;

    void request(std::string url);
    void processCompleted();
    [[nodiscard]] std::optional<std::string> localPath(std::string_view url) const;
    [[nodiscard]] std::size_t filesystemProbeCount() const;
    void retryFailed();
    void shutdown();

private:
    RaHttpTransport& transport_;
    std::string cacheDirectory_;
    bool stopping_ = false;

    struct State;
    std::unique_ptr<State> state_;
};

void applyCachedImagePaths(
    RaSessionSnapshot& snapshot,
    const RetroAchievementsImageCache& cache
);
void applyCachedImagePathsForUrls(
    RaSessionSnapshot& snapshot,
    const RetroAchievementsImageCache& cache,
    const std::vector<std::string>& urls
);

} // namespace gb::frontend
