#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rc_client.h"

#include "gb/app/frontend/realtime/retroachievements_config.hpp"
#include "gb/app/frontend/realtime/retroachievements_models.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"

namespace gb {
class GameBoy;
}

namespace gb::frontend {

class RaHttpTransport;

class RaClientApi {
public:
    virtual ~RaClientApi() = default;

    virtual rc_client_t* create(
        rc_client_read_memory_func_t readMemory,
        rc_client_server_call_t serverCall
    );
    // The session completes outstanding server callbacks before this call. On
    // return, no callback associated with the destroyed client may run again.
    virtual void destroy(rc_client_t* client);
    virtual void setUserdata(rc_client_t* client, void* userdata);
    virtual void setEventHandler(rc_client_t* client, rc_client_event_handler_t handler);
    virtual void setHardcoreEnabled(rc_client_t* client, int enabled);

    virtual rc_client_async_handle_t* beginLoginWithPassword(
        rc_client_t* client,
        // The implementation may observe but must not retain these strings.
        const char* username,
        const char* password,
        rc_client_callback_t callback,
        void* callbackUserdata
    );
    virtual rc_client_async_handle_t* beginLoginWithToken(
        rc_client_t* client,
        // The implementation may observe but must not retain these strings.
        const char* username,
        const char* token,
        rc_client_callback_t callback,
        void* callbackUserdata
    );
    // Lifecycle seam invoked after beginLogin has consumed the logical secret,
    // while its stable buffer is still alive and already contains only zeroes.
    virtual void onLoginSecretWiped(
        const char* logicalBuffer,
        std::size_t logicalSize,
        std::size_t storageSize
    );
    virtual void logout(rc_client_t* client);
    [[nodiscard]] virtual const rc_client_user_t* getUserInfo(
        const rc_client_t* client
    ) const;
    [[nodiscard]] virtual int userGetImageUrl(
        const rc_client_user_t* user,
        char* buffer,
        std::size_t bufferSize
    ) const;

    virtual rc_client_async_handle_t* beginFetchAllUserProgress(
        rc_client_t* client,
        std::uint32_t consoleId,
        rc_client_fetch_all_user_progress_callback_t callback,
        void* callbackUserdata
    );
    virtual void destroyAllUserProgress(rc_client_all_user_progress_t* list);
    virtual rc_client_async_handle_t* beginFetchGameTitles(
        rc_client_t* client,
        const std::uint32_t* gameIds,
        std::uint32_t numGameIds,
        rc_client_fetch_game_titles_callback_t callback,
        void* callbackUserdata
    );
    virtual void destroyGameTitleList(rc_client_game_title_list_t* list);

    virtual rc_client_async_handle_t* beginIdentifyAndLoadGame(
        rc_client_t* client,
        std::uint32_t consoleId,
        const char* filePath,
        const std::uint8_t* data,
        std::size_t dataSize,
        rc_client_callback_t callback,
        void* callbackUserdata
    );
    [[nodiscard]] virtual bool isGameLoaded(const rc_client_t* client) const;
    [[nodiscard]] virtual const rc_client_game_t* getGameInfo(
        const rc_client_t* client
    ) const;
    [[nodiscard]] virtual int gameGetImageUrl(
        const rc_client_game_t* game,
        char* buffer,
        std::size_t bufferSize
    ) const;
    virtual void getUserGameSummary(
        const rc_client_t* client,
        rc_client_user_game_summary_t* summary
    ) const;
    virtual rc_client_achievement_list_t* createAchievementList(
        rc_client_t* client,
        int category,
        int grouping
    );
    virtual void destroyAchievementList(rc_client_achievement_list_t* list);
    [[nodiscard]] virtual int achievementGetImageUrl(
        const rc_client_achievement_t* achievement,
        int state,
        char* buffer,
        std::size_t bufferSize
    ) const;

    virtual void doFrame(rc_client_t* client);
    virtual void idle(rc_client_t* client);
    virtual void reset(rc_client_t* client);
    [[nodiscard]] virtual std::size_t progressSize(rc_client_t* client) const;
    [[nodiscard]] virtual int serializeProgressSized(
        rc_client_t* client,
        std::uint8_t* buffer,
        std::size_t bufferSize
    ) const;
    [[nodiscard]] virtual int deserializeProgressSized(
        rc_client_t* client,
        const std::uint8_t* buffer,
        std::size_t bufferSize
    );
};

using RaConfigPersistence = std::function<bool(const RaConfig&)>;
using RaMemoryReader = std::function<std::uint32_t(
    std::uint32_t,
    std::uint8_t*,
    std::uint32_t
)>;

class RetroAchievementsSession {
public:
    RetroAchievementsSession(
        gb::GameBoy& gameBoy,
        RaHttpTransport& transport,
        RaConfig config = {},
        RaConfigPersistence persistConfig = {},
        RaClientApi* clientApi = nullptr
    );
    RetroAchievementsSession(
        RaMemoryReader memoryReader,
        std::uint32_t defaultConsoleId,
        RaHttpTransport& transport,
        RaConfig config = {},
        RaConfigPersistence persistConfig = {},
        RaClientApi* clientApi = nullptr
    );
    // Destroy on the creating thread, or call shutdown successfully there first.
    ~RetroAchievementsSession();

    RetroAchievementsSession(const RetroAchievementsSession&) = delete;
    RetroAchievementsSession& operator=(const RetroAchievementsSession&) = delete;
    RetroAchievementsSession(RetroAchievementsSession&&) = delete;
    RetroAchievementsSession& operator=(RetroAchievementsSession&&) = delete;

    void enqueueLogin(std::string username, std::string_view password);
    void enqueueLogin(std::string username, RaSecretString password);
    void enqueueTokenLogin(std::string username, std::string_view token);
    void enqueueTokenLogin(std::string username, RaSecretString token);
    void enqueueLogout();
    void enqueueLoadGame(std::uint32_t consoleId, std::string romPath);

    void processPending();
    void doFrame();
    void idle();
    [[nodiscard]] RaSessionSnapshot snapshot() const;
    [[nodiscard]] std::vector<RaUiEvent> takeEvents();

    [[nodiscard]] std::vector<std::uint8_t> serializeProgress() const;
    [[nodiscard]] bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    );
    [[nodiscard]] bool resetProgress();
    // Returns false without mutation off the creating thread. Owner-thread
    // shutdown is idempotent and returns true.
    bool shutdown();

private:
    static std::uint32_t RC_CCONV readMemoryThunk(
        std::uint32_t address,
        std::uint8_t* buffer,
        std::uint32_t numBytes,
        rc_client_t* client
    );
    static void RC_CCONV serverCallThunk(
        const rc_api_request_t* request,
        rc_client_server_callback_t callback,
        void* callbackData,
        rc_client_t* client
    );
    static void RC_CCONV eventThunk(
        const rc_client_event_t* event,
        rc_client_t* client
    );
    static void RC_CCONV loginThunk(
        int result,
        const char* errorMessage,
        rc_client_t* client,
        void* callbackData
    );
    static void RC_CCONV gameLoadThunk(
        int result,
        const char* errorMessage,
        rc_client_t* client,
        void* callbackData
    );
    static void RC_CCONV gbProgressThunk(
        int result,
        const char* errorMessage,
        rc_client_all_user_progress_t* list,
        rc_client_t* client,
        void* callbackData
    );
    static void RC_CCONV gbaProgressThunk(
        int result,
        const char* errorMessage,
        rc_client_all_user_progress_t* list,
        rc_client_t* client,
        void* callbackData
    );
    static void RC_CCONV gbcProgressThunk(
        int result,
        const char* errorMessage,
        rc_client_all_user_progress_t* list,
        rc_client_t* client,
        void* callbackData
    );
    static void RC_CCONV titlesThunk(
        int result,
        const char* errorMessage,
        rc_client_game_title_list_t* list,
        rc_client_t* client,
        void* callbackData
    );

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gb::frontend
