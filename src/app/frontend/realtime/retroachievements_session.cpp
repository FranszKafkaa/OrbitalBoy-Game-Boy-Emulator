#include "gb/app/frontend/realtime/retroachievements_session.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gb/app/frontend/realtime/retroachievements_http.hpp"
#include "gb/app/frontend/realtime/retroachievements_memory.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"
#include "gb/core/gameboy.hpp"

namespace gb::frontend {

rc_client_t* RaClientApi::create(
    rc_client_read_memory_func_t readMemory,
    rc_client_server_call_t serverCall
) {
    return rc_client_create(readMemory, serverCall);
}

void RaClientApi::destroy(rc_client_t* client) {
    rc_client_destroy(client);
}

void RaClientApi::setUserdata(rc_client_t* client, void* userdata) {
    rc_client_set_userdata(client, userdata);
}

void RaClientApi::setEventHandler(
    rc_client_t* client,
    rc_client_event_handler_t handler
) {
    rc_client_set_event_handler(client, handler);
}

void RaClientApi::setHardcoreEnabled(rc_client_t* client, int enabled) {
    rc_client_set_hardcore_enabled(client, enabled);
}

rc_client_async_handle_t* RaClientApi::beginLoginWithPassword(
    rc_client_t* client,
    const char* username,
    const char* password,
    rc_client_callback_t callback,
    void* callbackUserdata
) {
    return rc_client_begin_login_with_password(
        client,
        username,
        password,
        callback,
        callbackUserdata
    );
}

rc_client_async_handle_t* RaClientApi::beginLoginWithToken(
    rc_client_t* client,
    const char* username,
    const char* token,
    rc_client_callback_t callback,
    void* callbackUserdata
) {
    return rc_client_begin_login_with_token(
        client,
        username,
        token,
        callback,
        callbackUserdata
    );
}

void RaClientApi::onLoginSecretWiped(
    const char* logicalBuffer,
    std::size_t logicalSize,
    std::size_t storageSize
) {
    static_cast<void>(logicalBuffer);
    static_cast<void>(logicalSize);
    static_cast<void>(storageSize);
}

void RaClientApi::logout(rc_client_t* client) {
    rc_client_logout(client);
}

const rc_client_user_t* RaClientApi::getUserInfo(
    const rc_client_t* client
) const {
    return rc_client_get_user_info(client);
}

int RaClientApi::userGetImageUrl(
    const rc_client_user_t* user,
    char* buffer,
    std::size_t bufferSize
) const {
    return rc_client_user_get_image_url(user, buffer, bufferSize);
}

rc_client_async_handle_t* RaClientApi::beginFetchAllUserProgress(
    rc_client_t* client,
    std::uint32_t consoleId,
    rc_client_fetch_all_user_progress_callback_t callback,
    void* callbackUserdata
) {
    return rc_client_begin_fetch_all_user_progress(
        client,
        consoleId,
        callback,
        callbackUserdata
    );
}

void RaClientApi::destroyAllUserProgress(
    rc_client_all_user_progress_t* list
) {
    rc_client_destroy_all_user_progress(list);
}

rc_client_async_handle_t* RaClientApi::beginFetchGameTitles(
    rc_client_t* client,
    const std::uint32_t* gameIds,
    std::uint32_t numGameIds,
    rc_client_fetch_game_titles_callback_t callback,
    void* callbackUserdata
) {
    return rc_client_begin_fetch_game_titles(
        client,
        gameIds,
        numGameIds,
        callback,
        callbackUserdata
    );
}

void RaClientApi::destroyGameTitleList(rc_client_game_title_list_t* list) {
    rc_client_destroy_game_title_list(list);
}

rc_client_async_handle_t* RaClientApi::beginIdentifyAndLoadGame(
    rc_client_t* client,
    std::uint32_t consoleId,
    const char* filePath,
    const std::uint8_t* data,
    std::size_t dataSize,
    rc_client_callback_t callback,
    void* callbackUserdata
) {
    return rc_client_begin_identify_and_load_game(
        client,
        consoleId,
        filePath,
        data,
        dataSize,
        callback,
        callbackUserdata
    );
}

bool RaClientApi::isGameLoaded(const rc_client_t* client) const {
    return rc_client_is_game_loaded(client) != 0;
}

const rc_client_game_t* RaClientApi::getGameInfo(
    const rc_client_t* client
) const {
    return rc_client_get_game_info(client);
}

int RaClientApi::gameGetImageUrl(
    const rc_client_game_t* game,
    char* buffer,
    std::size_t bufferSize
) const {
    return rc_client_game_get_image_url(game, buffer, bufferSize);
}

void RaClientApi::getUserGameSummary(
    const rc_client_t* client,
    rc_client_user_game_summary_t* summary
) const {
    rc_client_get_user_game_summary(client, summary);
}

rc_client_achievement_list_t* RaClientApi::createAchievementList(
    rc_client_t* client,
    int category,
    int grouping
) {
    return rc_client_create_achievement_list(client, category, grouping);
}

void RaClientApi::destroyAchievementList(
    rc_client_achievement_list_t* list
) {
    rc_client_destroy_achievement_list(list);
}

int RaClientApi::achievementGetImageUrl(
    const rc_client_achievement_t* achievement,
    int state,
    char* buffer,
    std::size_t bufferSize
) const {
    return rc_client_achievement_get_image_url(
        achievement,
        state,
        buffer,
        bufferSize
    );
}

void RaClientApi::doFrame(rc_client_t* client) {
    rc_client_do_frame(client);
}

void RaClientApi::idle(rc_client_t* client) {
    rc_client_idle(client);
}

void RaClientApi::reset(rc_client_t* client) {
    rc_client_reset(client);
}

std::size_t RaClientApi::progressSize(rc_client_t* client) const {
    return rc_client_progress_size(client);
}

int RaClientApi::serializeProgressSized(
    rc_client_t* client,
    std::uint8_t* buffer,
    std::size_t bufferSize
) const {
    return rc_client_serialize_progress_sized(client, buffer, bufferSize);
}

int RaClientApi::deserializeProgressSized(
    rc_client_t* client,
    const std::uint8_t* buffer,
    std::size_t bufferSize
) {
    return rc_client_deserialize_progress_sized(client, buffer, bufferSize);
}

namespace {

constexpr std::size_t kMaximumUiEvents = 32;
constexpr std::size_t kMaximumTitleBatch = 100;
constexpr std::uint32_t kGameBoyConsoleId = 4;
constexpr std::uint32_t kGameBoyAdvanceConsoleId = 5;
constexpr std::uint32_t kGameBoyColorConsoleId = 6;

using RaConsoleIdProvider = std::function<std::uint32_t()>;

std::mutex gSessionRegistryMutex;
std::unordered_map<rc_client_t*, RetroAchievementsSession*> gSessionRegistry;
std::atomic<std::uint64_t> gNextRequestId{1};

void registerSession(
    rc_client_t* client,
    RetroAchievementsSession* session
) {
    std::lock_guard lock(gSessionRegistryMutex);
    gSessionRegistry[client] = session;
}

void unregisterSession(rc_client_t* client) {
    std::lock_guard lock(gSessionRegistryMutex);
    gSessionRegistry.erase(client);
}

RetroAchievementsSession* sessionForClient(rc_client_t* client) {
    std::lock_guard lock(gSessionRegistryMutex);
    const auto found = gSessionRegistry.find(client);
    return found == gSessionRegistry.end() ? nullptr : found->second;
}

std::string copyText(const char* text) {
    return text ? text : "";
}

std::string lowercaseAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }
    return lowered;
}

bool isTransientLoginFailure(int result) {
    return result == RC_NO_RESPONSE;
}

std::string imageUrl(
    const char* directUrl,
    const std::function<int(char*, std::size_t)>& fallback
) {
    if (directUrl && directUrl[0]) {
        return directUrl;
    }

    std::array<char, 2048> buffer{};
    if (fallback(buffer.data(), buffer.size()) == RC_OK) {
        return buffer.data();
    }
    return {};
}

} // namespace

class RetroAchievementsSession::Impl {
public:
    enum class CommandType {
        PasswordLogin,
        TokenLogin,
        Logout,
        LoadGame,
    };

    struct Command {
        CommandType type = CommandType::Logout;
        std::string username;
        RaSecretString secret;
        std::string romPath;
        std::uint32_t consoleId = 0;
    };

    struct PendingServerCall {
        rc_client_server_callback_t callback = nullptr;
        void* callbackData = nullptr;
    };

    struct RejectedServerCall {
        rc_client_server_callback_t callback = nullptr;
        void* callbackData = nullptr;
    };

    struct LoginCallbackContext {
        RetroAchievementsSession* session = nullptr;
        std::uint64_t generation = 0;
    };

    struct GameCallbackContext {
        RetroAchievementsSession* session = nullptr;
        std::uint64_t generation = 0;
    };

    struct ProfileCallbackContext {
        RetroAchievementsSession* session = nullptr;
        std::uint64_t generation = 0;
    };

    struct TitleCallbackContext {
        RetroAchievementsSession* session = nullptr;
        std::uint64_t generation = 0;
    };

    Impl(
        RetroAchievementsSession& ownerValue,
        RaMemoryReader memoryReaderValue,
        RaConsoleIdProvider defaultConsoleIdValue,
        RaHttpTransport& transportValue,
        RaConfig configValue,
        RaConfigPersistence persistConfigValue,
        RaClientApi* clientApi
    )
        : owner(ownerValue),
          memoryReader(std::move(memoryReaderValue)),
          defaultConsoleId(std::move(defaultConsoleIdValue)),
          transport(transportValue),
          config(std::move(configValue)),
          persistConfig(std::move(persistConfigValue)),
          ownerThread(std::this_thread::get_id()) {
        config.clearToken();
        config.username.clear();
        if (clientApi) {
            api = clientApi;
        } else {
            ownedApi = std::make_unique<RaClientApi>();
            api = ownedApi.get();
        }

        state.connectionState = RaConnectionState::LoggedOut;
        state.statusText = "Desconectado do RetroAchievements.";
        client = api->create(
            &RetroAchievementsSession::readMemoryThunk,
            &RetroAchievementsSession::serverCallThunk
        );
        if (!client) {
            state.connectionState = RaConnectionState::Error;
            state.statusText = "RetroAchievements indisponível.";
            state.errorText = "Não foi possível iniciar o RetroAchievements.";
        } else {
            registerSession(client, &owner);
            api->setUserdata(client, &owner);
            api->setEventHandler(client, &RetroAchievementsSession::eventThunk);
            api->setHardcoreEnabled(client, 0);
        }
        publishSnapshot();
    }

    ~Impl() {
        if (!shutdownRequested) {
            if (!onOwnerThread()) {
                std::terminate();
            }
            static_cast<void>(shutdown());
        }
    }

    [[nodiscard]] bool onOwnerThread() const {
        return std::this_thread::get_id() == ownerThread;
    }

    void enqueue(std::unique_ptr<Command> command) {
        std::lock_guard lock(commandMutex);
        if (shutdownRequested) {
            if (command->type == CommandType::PasswordLogin
                || command->type == CommandType::TokenLogin) {
                notifySecretWiped(*command);
            }
            return;
        }
        commands.push_back(std::move(command));
    }

    void processPending() {
        if (!onOwnerThread() || shutdownRequested || !client) {
            return;
        }

        processCommands();
        flushRejectedServerCalls();
        auto responses = transport.takeCompleted(RaHttpChannel::Api);
        for (auto& response : responses) {
            processResponse(std::move(response));
        }
        flushRejectedServerCalls();
    }

    void processCommands() {
        std::deque<std::unique_ptr<Command>> localCommands;
        {
            std::lock_guard lock(commandMutex);
            localCommands.swap(commands);
        }

        while (!localCommands.empty()) {
            auto command = std::move(localCommands.front());
            localCommands.pop_front();
            switch (command->type) {
            case CommandType::PasswordLogin:
                beginLogin(command->username, command->secret, false);
                notifySecretWiped(*command);
                break;
            case CommandType::TokenLogin:
                beginLogin(command->username, command->secret, true);
                notifySecretWiped(*command);
                break;
            case CommandType::Logout:
                logout();
                break;
            case CommandType::LoadGame:
                beginLoadGame(command->consoleId, command->romPath);
                break;
            }
        }
    }

    void notifySecretWiped(Command& command) {
        const std::size_t logicalSize = command.secret.size();
        command.secret.clear(
            [&](const char* storage, std::size_t storageSize) {
                api->onLoginSecretWiped(
                    storage,
                    logicalSize,
                    storageSize
                );
            }
        );
    }

    void beginLogin(
        const std::string& username,
        const RaSecretString& secret,
        bool tokenLogin
    ) {
        const std::uint64_t generation = ++loginGeneration;
        auto* context = new LoginCallbackContext{&owner, generation};
        loginContexts.insert(context);
        state.connectionState = RaConnectionState::LoggingIn;
        state.statusText = "Entrando no RetroAchievements...";
        state.errorText.clear();
        publishSnapshot();

        if (tokenLogin) {
            api->beginLoginWithToken(
                client,
                username.c_str(),
                secret.c_str(),
                &RetroAchievementsSession::loginThunk,
                context
            );
        } else {
            api->beginLoginWithPassword(
                client,
                username.c_str(),
                secret.c_str(),
                &RetroAchievementsSession::loginThunk,
                context
            );
        }
    }

    void loginCompleted(LoginCallbackContext* context, int result) {
        const auto contextFound = loginContexts.find(context);
        if (contextFound == loginContexts.end()) {
            return;
        }
        const std::uint64_t generation = context->generation;
        loginContexts.erase(contextFound);
        delete context;
        if (shutdownRequested || generation != loginGeneration
            || state.connectionState != RaConnectionState::LoggingIn) {
            return;
        }

        const rc_client_user_t* user = result == RC_OK
            ? api->getUserInfo(client)
            : nullptr;
        if (result != RC_OK || !user) {
            state.connectionState = RaConnectionState::Error;
            state.statusText = "Falha no login do RetroAchievements.";
            state.errorText = isTransientLoginFailure(result)
                ? "Não foi possível conectar ao RetroAchievements. Tente novamente."
                : "Usuário, senha ou token inválido no RetroAchievements.";
            if (!isTransientLoginFailure(result)) {
                config.clearToken();
                if (!persist()) {
                    state.errorText =
                        "Login recusado e não foi possível atualizar as credenciais locais.";
                }
            }
            publishSnapshot();
            addEvent({
                RaUiEventType::LoginFailed,
                "Falha no login",
                state.errorText,
                0,
                {},
            });
            return;
        }

        copyUser(*user);
        config.username = copyText(user->username);
        config.clearToken();
        config.assignToken(
            user->token ? std::string_view(user->token) : std::string_view{}
        );
        const bool configPersisted = persist();
        config.clearToken();
        config.username.clear();
        state.connectionState = RaConnectionState::Online;
        state.statusText = configPersisted
            ? "Conectado ao RetroAchievements."
            : "Conectado; credenciais não foram salvas.";
        state.errorText = configPersisted
            ? std::string{}
            : "Não foi possível salvar as credenciais do RetroAchievements.";
        publishSnapshot();
        addEvent({
            RaUiEventType::LoginSucceeded,
            "RetroAchievements conectado",
            state.profile.user.displayName,
            0,
            {},
        });
        beginFullProfile();
    }

    void logout() {
        ++loginGeneration;
        ++gameGeneration;
        ++profileGeneration;
        api->logout(client);
        config.username.clear();
        config.clearToken();
        const bool configPersisted = persist();

        state = {};
        state.connectionState = RaConnectionState::LoggedOut;
        state.statusText = configPersisted
            ? "Desconectado do RetroAchievements."
            : "Desconectado; arquivo de credenciais não foi atualizado.";
        if (!configPersisted) {
            state.errorText =
                "As credenciais foram removidas da sessão, mas o arquivo local não pôde ser atualizado.";
        }
        currentRomHash.clear();
        state.romHash.clear();
        mergedLibrary.clear();
        pendingProfileRequests = 0;
        pendingTitleRequests = 0;
        publishSnapshot();
    }

    void beginLoadGame(std::uint32_t consoleId, const std::string& romPath) {
        const std::uint64_t generation = ++gameGeneration;
        if (romPath.empty()) {
            state.errorText = "Não foi possível identificar o jogo no RetroAchievements.";
            publishSnapshot();
            return;
        }
        if (consoleId != kGameBoyConsoleId
            && consoleId != kGameBoyAdvanceConsoleId
            && consoleId != kGameBoyColorConsoleId) {
            consoleId = defaultConsoleId ? defaultConsoleId() : 0U;
        }
        if (consoleId != kGameBoyConsoleId
            && consoleId != kGameBoyAdvanceConsoleId
            && consoleId != kGameBoyColorConsoleId) {
            state.errorText = "Sistema não suportado pelo RetroAchievements.";
            publishSnapshot();
            return;
        }

        auto* context = new GameCallbackContext{&owner, generation};
        gameContexts.insert(context);
        state.errorText.clear();
        api->beginIdentifyAndLoadGame(
            client,
            consoleId,
            romPath.c_str(),
            nullptr,
            0,
            &RetroAchievementsSession::gameLoadThunk,
            context
        );
    }

    void gameLoadCompleted(GameCallbackContext* context, int result) {
        const auto contextFound = gameContexts.find(context);
        if (contextFound == gameContexts.end()) {
            return;
        }
        const std::uint64_t generation = context->generation;
        gameContexts.erase(contextFound);
        delete context;
        if (shutdownRequested || generation != gameGeneration) {
            return;
        }
        if (result != RC_OK || !api->isGameLoaded(client)) {
            if (state.connectionState == RaConnectionState::Offline) {
                return;
            }
            state.gameLoaded = false;
            state.currentGame = {};
            state.currentAchievements.clear();
            currentRomHash.clear();
            state.romHash.clear();
            state.errorText =
                "O jogo continua disponível, mas não foi identificado no RetroAchievements.";
            publishSnapshot();
            return;
        }

        const rc_client_game_t* game = api->getGameInfo(client);
        currentRomHash = game ? copyText(game->hash) : "";
        state.romHash = currentRomHash;
        refreshCurrentGame();
        publishSnapshot();
        addEvent({
            RaUiEventType::GameLoaded,
            "Jogo conectado",
            state.currentGame.title,
            0,
            {},
        });
    }

    void beginFullProfile() {
        ++profileGeneration;
        mergedLibrary.clear();
        state.profile.library.clear();
        pendingProfileRequests = 2;
        pendingTitleRequests = 0;
        profileWarning = false;

        beginConsoleProfile(kGameBoyConsoleId, &RetroAchievementsSession::gbProgressThunk);
        beginConsoleProfile(
            kGameBoyColorConsoleId,
            &RetroAchievementsSession::gbcProgressThunk
        );
    }

    void beginConsoleProfile(
        std::uint32_t consoleId,
        rc_client_fetch_all_user_progress_callback_t callback
    ) {
        auto* context = new ProfileCallbackContext{
            &owner,
            profileGeneration,
        };
        profileContexts.insert(context);
        api->beginFetchAllUserProgress(
            client,
            consoleId,
            callback,
            context
        );
    }

    void profileCompleted(
        ProfileCallbackContext* context,
        int result,
        rc_client_all_user_progress_t* list
    ) {
        const auto contextFound = profileContexts.find(context);
        if (contextFound == profileContexts.end()) {
            if (list) {
                api->destroyAllUserProgress(list);
            }
            return;
        }
        const auto generation = context->generation;
        profileContexts.erase(contextFound);
        delete context;

        const bool active = !shutdownRequested
            && generation == profileGeneration
            && (state.connectionState == RaConnectionState::Online
                || state.connectionState == RaConnectionState::Offline);
        if (active && result == RC_OK && list) {
            for (std::uint32_t index = 0; index < list->num_entries; ++index) {
                const auto& source = list->entries[index];
                auto& target = mergedLibrary[source.game_id];
                target.gameId = source.game_id;
                target.total = std::max(target.total, source.num_achievements);
                target.unlockedCasual = std::max(
                    target.unlockedCasual,
                    source.num_unlocked_achievements
                );
                target.unlockedHardcore = std::max(
                    target.unlockedHardcore,
                    source.num_unlocked_achievements_hardcore
                );
            }
        } else if (active && result != RC_OK) {
            profileWarning = true;
        }

        if (list) {
            api->destroyAllUserProgress(list);
        }
        if (!active) {
            return;
        }
        if (pendingProfileRequests > 0) {
            --pendingProfileRequests;
        }
        if (pendingProfileRequests == 0) {
            beginTitleBatches();
        }
    }

    void beginTitleBatches() {
        std::vector<std::uint32_t> gameIds;
        gameIds.reserve(mergedLibrary.size());
        for (const auto& [gameId, unused] : mergedLibrary) {
            static_cast<void>(unused);
            gameIds.push_back(gameId);
        }
        std::sort(gameIds.begin(), gameIds.end());

        if (gameIds.empty()) {
            finishProfile();
            return;
        }

        pendingTitleRequests =
            (gameIds.size() + kMaximumTitleBatch - 1) / kMaximumTitleBatch;
        for (std::size_t offset = 0; offset < gameIds.size();
             offset += kMaximumTitleBatch) {
            const auto count = std::min(
                kMaximumTitleBatch,
                gameIds.size() - offset
            );
            auto* context = new TitleCallbackContext{
                &owner,
                profileGeneration,
            };
            titleContexts.insert(context);
            api->beginFetchGameTitles(
                client,
                gameIds.data() + offset,
                static_cast<std::uint32_t>(count),
                &RetroAchievementsSession::titlesThunk,
                context
            );
        }
    }

    void titlesCompleted(
        TitleCallbackContext* context,
        int result,
        rc_client_game_title_list_t* list
    ) {
        const auto contextFound = titleContexts.find(context);
        if (contextFound == titleContexts.end()) {
            if (list) {
                api->destroyGameTitleList(list);
            }
            return;
        }
        const auto generation = context->generation;
        titleContexts.erase(contextFound);
        delete context;

        const bool active = !shutdownRequested
            && generation == profileGeneration
            && (state.connectionState == RaConnectionState::Online
                || state.connectionState == RaConnectionState::Offline);
        if (active && result == RC_OK && list) {
            for (std::uint32_t index = 0; index < list->num_entries; ++index) {
                const auto& source = list->entries[index];
                const auto found = mergedLibrary.find(source.game_id);
                if (found == mergedLibrary.end()) {
                    continue;
                }
                found->second.title = copyText(source.title);
                found->second.badgeUrl = copyText(source.badge_url);
            }
        } else if (active && result != RC_OK) {
            profileWarning = true;
        }

        if (list) {
            api->destroyGameTitleList(list);
        }
        if (!active) {
            return;
        }
        if (pendingTitleRequests > 0) {
            --pendingTitleRequests;
        }
        if (pendingTitleRequests == 0) {
            finishProfile();
        }
    }

    void finishProfile() {
        state.profile.library.clear();
        state.profile.library.reserve(mergedLibrary.size());
        for (auto& [gameId, game] : mergedLibrary) {
            static_cast<void>(gameId);
            state.profile.library.push_back(std::move(game));
        }
        std::sort(
            state.profile.library.begin(),
            state.profile.library.end(),
            [](const RaGameProgressSummary& left, const RaGameProgressSummary& right) {
                const std::string leftTitle = lowercaseAscii(left.title);
                const std::string rightTitle = lowercaseAscii(right.title);
                if (leftTitle != rightTitle) {
                    return leftTitle < rightTitle;
                }
                return left.gameId < right.gameId;
            }
        );
        state.errorText = profileWarning
            ? "Parte da biblioteca RetroAchievements não pôde ser atualizada."
            : "";
        publishSnapshot();
    }

    void refreshUser() {
        const rc_client_user_t* user = api->getUserInfo(client);
        if (user) {
            copyUser(*user);
        }
    }

    void copyUser(const rc_client_user_t& user) {
        state.profile.user.username = copyText(user.username);
        state.profile.user.displayName = copyText(user.display_name);
        state.profile.user.scoreHardcore = user.score;
        state.profile.user.scoreCasual = user.score_softcore;
        state.profile.user.unreadMessages = user.num_unread_messages;
        state.profile.user.avatarUrl = imageUrl(
            nullptr,
            [&](char* buffer, std::size_t size) {
                return api->userGetImageUrl(&user, buffer, size);
            }
        );
        if (state.profile.user.avatarUrl.empty()) {
            state.profile.user.avatarUrl = copyText(user.avatar_url);
        }
    }

    RaAchievementSummary copyAchievement(
        const rc_client_achievement_t& achievement
    ) const {
        RaAchievementSummary summary;
        summary.id = achievement.id;
        summary.title = copyText(achievement.title);
        summary.description = copyText(achievement.description);
        summary.points = achievement.points;
        summary.unlocked =
            achievement.state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED
            || achievement.unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
        summary.measuredProgress = achievement.measured_progress;
        summary.badgeUrl = imageUrl(
            achievement.badge_url,
            [&](char* buffer, std::size_t size) {
                return api->achievementGetImageUrl(
                    &achievement,
                    summary.unlocked
                        ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED
                        : RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE,
                    buffer,
                    size
                );
            }
        );
        return summary;
    }

    void refreshCurrentGame() {
        const rc_client_game_t* game = api->getGameInfo(client);
        if (!game || !api->isGameLoaded(client)) {
            state.currentGame = {};
            state.currentAchievements.clear();
            state.gameLoaded = false;
            return;
        }

        state.gameLoaded = true;
        state.currentGame = {};
        state.currentGame.gameId = game->id;
        state.currentGame.title = copyText(game->title);
        state.currentGame.badgeUrl = imageUrl(
            game->badge_url,
            [&](char* buffer, std::size_t size) {
                return api->gameGetImageUrl(game, buffer, size);
            }
        );

        rc_client_user_game_summary_t summary{};
        api->getUserGameSummary(client, &summary);
        state.currentGame.total = summary.num_core_achievements;
        state.currentGame.unlockedCasual = summary.num_unlocked_achievements;

        state.currentAchievements.clear();
        std::uint32_t hardcoreUnlocks = 0;
        rc_client_achievement_list_t* list = api->createAchievementList(
            client,
            RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS
        );
        if (list) {
            for (std::uint32_t bucketIndex = 0;
                 bucketIndex < list->num_buckets;
                 ++bucketIndex) {
                const auto& bucket = list->buckets[bucketIndex];
                for (std::uint32_t achievementIndex = 0;
                     achievementIndex < bucket.num_achievements;
                     ++achievementIndex) {
                    const rc_client_achievement_t* achievement =
                        bucket.achievements[achievementIndex];
                    if (!achievement) {
                        continue;
                    }
                    state.currentAchievements.push_back(
                        copyAchievement(*achievement)
                    );
                    if ((achievement->unlocked
                         & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0) {
                        ++hardcoreUnlocks;
                    }
                }
            }
            api->destroyAchievementList(list);
        }
        state.currentGame.unlockedHardcore = hardcoreUnlocks;
        if (state.currentGame.total == 0) {
            state.currentGame.total = static_cast<std::uint32_t>(
                state.currentAchievements.size()
            );
        }
    }

    void achievementTriggered(const rc_client_event_t& event) {
        if (shutdownRequested
            || !state.gameLoaded
            || event.type != RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED
            || !event.achievement) {
            return;
        }

        const auto achievement = copyAchievement(*event.achievement);
        refreshCurrentGame();
        refreshUser();
        publishSnapshot();
        addEvent({
            RaUiEventType::AchievementUnlocked,
            achievement.title,
            achievement.description,
            achievement.points,
            {},
        });
    }

    void handleClientEvent(const rc_client_event_t& event) {
        if (shutdownRequested) {
            return;
        }
        if (event.type == RC_CLIENT_EVENT_DISCONNECTED) {
            markOfflineForTransportFailure();
        } else if (event.type == RC_CLIENT_EVENT_RECONNECTED) {
            markOnlineAfterReconnect();
        } else if (event.type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED) {
            achievementTriggered(event);
        }
    }

    void handleServerCall(
        const rc_api_request_t* request,
        rc_client_server_callback_t callback,
        void* callbackData
    ) {
        if (!onOwnerThread() || shutdownRequested || !callback || !request) {
            rc_api_server_response_t error{
                "",
                0,
                RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
            };
            if (callback) {
                callback(&error, callbackData);
            }
            return;
        }

        const std::uint64_t requestId =
            gNextRequestId.fetch_add(1, std::memory_order_relaxed);
        pendingServerCalls.emplace(
            requestId,
            PendingServerCall{callback, callbackData}
        );
        RaHttpRequest transportRequest;
        transportRequest.id = requestId;
        transportRequest.channel = RaHttpChannel::Api;
        transportRequest.url = copyText(request->url);
        transportRequest.postData = copyText(request->post_data);
        if (!transport.submit(std::move(transportRequest))) {
            pendingServerCalls.erase(requestId);
            rejectedServerCalls.push_back({callback, callbackData});
        }
    }

    void flushRejectedServerCalls() {
        while (!rejectedServerCalls.empty() && !shutdownRequested) {
            const RejectedServerCall rejected = rejectedServerCalls.front();
            rejectedServerCalls.pop_front();
            markOfflineForTransportFailure();
            rc_api_server_response_t error{
                "",
                0,
                RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
            };
            rejected.callback(&error, rejected.callbackData);
        }
    }

    void processResponse(RaHttpResponse response) {
        const auto found = pendingServerCalls.find(response.id);
        if (found == pendingServerCalls.end()) {
            return;
        }
        const PendingServerCall pending = found->second;
        pendingServerCalls.erase(found);

        if (!response.error.empty()) {
            markOfflineForTransportFailure();
        } else {
            markOnlineAfterReconnect();
        }

        const char* body = response.body.empty()
            ? ""
            : reinterpret_cast<const char*>(response.body.data());
        rc_api_server_response_t serverResponse{
            body,
            response.body.size(),
            response.error.empty()
                ? static_cast<int>(response.statusCode)
                : (response.statusCode > 0
                    ? static_cast<int>(response.statusCode)
                    : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR),
        };
        pending.callback(&serverResponse, pending.callbackData);
        volatile std::uint8_t* bytes = response.body.empty()
            ? nullptr
            : response.body.data();
        for (std::size_t index = 0; index < response.body.size(); ++index) {
            bytes[index] = 0;
        }
        std::vector<std::uint8_t>{}.swap(response.body);
    }

    void cancelServerCalls() {
        const rc_api_server_response_t cancellation{
            "",
            0,
            RC_API_SERVER_RESPONSE_CLIENT_ERROR,
        };
        while (!pendingServerCalls.empty()) {
            const auto found = pendingServerCalls.begin();
            const PendingServerCall pending = found->second;
            pendingServerCalls.erase(found);
            pending.callback(&cancellation, pending.callbackData);
        }
        while (!rejectedServerCalls.empty()) {
            const RejectedServerCall rejected = rejectedServerCalls.front();
            rejectedServerCalls.pop_front();
            rejected.callback(&cancellation, rejected.callbackData);
        }
    }

    void markOfflineForTransportFailure() {
        if (state.connectionState != RaConnectionState::Online) {
            return;
        }
        state.connectionState = RaConnectionState::Offline;
        state.statusText = "RetroAchievements offline.";
        state.errorText =
            "A conexão com o RetroAchievements foi interrompida; o jogo continua disponível.";
        publishSnapshot();
        addEvent({
            RaUiEventType::Offline,
            "RetroAchievements offline",
            state.errorText,
            0,
            {},
        });
    }

    void markOnlineAfterReconnect() {
        if (state.connectionState != RaConnectionState::Offline) {
            return;
        }
        state.connectionState = RaConnectionState::Online;
        ++state.connectionGeneration;
        state.statusText = "Conectado ao RetroAchievements.";
        state.errorText.clear();
        publishSnapshot();
        addEvent({
            RaUiEventType::Reconnected,
            "RetroAchievements reconectado",
            "A conexão foi restabelecida.",
            0,
            {},
        });
    }

    void addEvent(RaUiEvent event) {
        std::lock_guard lock(eventMutex);
        if (events.size() == kMaximumUiEvents) {
            events.pop_front();
        }
        events.push_back(std::move(event));
    }

    void publishSnapshot() {
        std::lock_guard lock(snapshotMutex);
        publishedSnapshot = state;
    }

    [[nodiscard]] RaSessionSnapshot snapshot() const {
        std::lock_guard lock(snapshotMutex);
        return publishedSnapshot;
    }

    [[nodiscard]] std::vector<RaUiEvent> takeEvents() {
        std::lock_guard lock(eventMutex);
        std::vector<RaUiEvent> result;
        result.reserve(events.size());
        std::move(events.begin(), events.end(), std::back_inserter(result));
        events.clear();
        return result;
    }

    [[nodiscard]] bool persist() {
        return !persistConfig || persistConfig(config);
    }

    [[nodiscard]] std::vector<std::uint8_t> serializeProgress() const {
        if (!onOwnerThread() || shutdownRequested || !client
            || !state.gameLoaded) {
            return {};
        }
        const std::size_t size = api->progressSize(client);
        if (size == 0) {
            return {};
        }
        std::vector<std::uint8_t> progress(size);
        if (api->serializeProgressSized(client, progress.data(), progress.size())
            != RC_OK) {
            return {};
        }
        return progress;
    }

    [[nodiscard]] bool deserializeProgress(
        std::string_view romHash,
        const std::vector<std::uint8_t>& payload
    ) {
        if (!onOwnerThread() || shutdownRequested || !client
            || !state.gameLoaded || payload.empty() || currentRomHash.empty()
            || romHash != currentRomHash) {
            return false;
        }
        return api->deserializeProgressSized(
            client,
            payload.data(),
            payload.size()
        ) == RC_OK && refreshAfterProgressRestore();
    }

    [[nodiscard]] bool refreshAfterProgressRestore() {
        refreshCurrentGame();
        publishSnapshot();
        return true;
    }

    [[nodiscard]] bool resetProgress() {
        if (!onOwnerThread() || shutdownRequested || !client
            || !state.gameLoaded) {
            return false;
        }
        api->reset(client);
        refreshCurrentGame();
        publishSnapshot();
        return true;
    }

    void doFrame() {
        if (!onOwnerThread() || shutdownRequested || !client) {
            return;
        }
        if (state.gameLoaded) {
            api->doFrame(client);
        }
    }

    void idle() {
        if (!onOwnerThread() || shutdownRequested || !client) {
            return;
        }
        processPending();
        api->idle(client);
    }

    [[nodiscard]] bool shutdown() {
        if (!onOwnerThread()) {
            return false;
        }

        {
            std::lock_guard lock(commandMutex);
            if (shutdownRequested) {
                return true;
            }
            shutdownRequested = true;
            for (auto& command : commands) {
                if (command->type == CommandType::PasswordLogin
                    || command->type == CommandType::TokenLogin) {
                    notifySecretWiped(*command);
                }
            }
            commands.clear();
        }

        cancelServerCalls();
        config.clearToken();
        config.username.clear();
        if (client) {
            api->destroy(client);
            unregisterSession(client);
            client = nullptr;
        }
        for (auto* context : loginContexts) {
            delete context;
        }
        loginContexts.clear();
        for (auto* context : gameContexts) {
            delete context;
        }
        gameContexts.clear();
        for (auto* context : profileContexts) {
            delete context;
        }
        profileContexts.clear();
        for (auto* context : titleContexts) {
            delete context;
        }
        titleContexts.clear();
        return true;
    }

    RetroAchievementsSession& owner;
    RaMemoryReader memoryReader;
    RaConsoleIdProvider defaultConsoleId;
    RaHttpTransport& transport;
    RaConfig config;
    RaConfigPersistence persistConfig;
    std::unique_ptr<RaClientApi> ownedApi;
    RaClientApi* api = nullptr;
    rc_client_t* client = nullptr;
    std::thread::id ownerThread;

    mutable std::mutex commandMutex;
    std::deque<std::unique_ptr<Command>> commands;
    mutable std::mutex snapshotMutex;
    RaSessionSnapshot publishedSnapshot;
    RaSessionSnapshot state;
    mutable std::mutex eventMutex;
    std::deque<RaUiEvent> events;

    std::unordered_map<std::uint64_t, PendingServerCall> pendingServerCalls;
    std::deque<RejectedServerCall> rejectedServerCalls;
    std::uint64_t loginGeneration = 0;
    std::unordered_set<LoginCallbackContext*> loginContexts;
    std::uint64_t gameGeneration = 0;
    std::unordered_set<GameCallbackContext*> gameContexts;
    std::uint64_t profileGeneration = 0;
    std::size_t pendingProfileRequests = 0;
    std::size_t pendingTitleRequests = 0;
    bool profileWarning = false;
    std::unordered_map<std::uint32_t, RaGameProgressSummary> mergedLibrary;
    std::unordered_set<ProfileCallbackContext*> profileContexts;
    std::unordered_set<TitleCallbackContext*> titleContexts;
    std::string currentRomHash;
    bool shutdownRequested = false;
};

RetroAchievementsSession::RetroAchievementsSession(
    gb::GameBoy& gameBoy,
    RaHttpTransport& transport,
    RaConfig config,
    RaConfigPersistence persistConfig,
    RaClientApi* clientApi
)
    : impl_(std::make_unique<Impl>(
          *this,
          [&gameBoy](
              std::uint32_t address,
              std::uint8_t* buffer,
              std::uint32_t numBytes
          ) {
              return readRetroAchievementsMemory(
                  gameBoy.bus(),
                  address,
                  buffer,
                  numBytes
              );
          },
          [&gameBoy]() {
              return gameBoy.runningInCgbMode()
                  ? kGameBoyColorConsoleId
                  : kGameBoyConsoleId;
          },
          transport,
          std::move(config),
          std::move(persistConfig),
          clientApi
      )) {}

RetroAchievementsSession::RetroAchievementsSession(
    RaMemoryReader memoryReader,
    std::uint32_t defaultConsoleId,
    RaHttpTransport& transport,
    RaConfig config,
    RaConfigPersistence persistConfig,
    RaClientApi* clientApi
)
    : impl_(std::make_unique<Impl>(
          *this,
          std::move(memoryReader),
          [defaultConsoleId]() { return defaultConsoleId; },
          transport,
          std::move(config),
          std::move(persistConfig),
          clientApi
      )) {}

RetroAchievementsSession::~RetroAchievementsSession() = default;

void RetroAchievementsSession::enqueueLogin(
    std::string username,
    std::string_view password
) {
    RaSecretString secret;
    secret.assign(password);
    enqueueLogin(std::move(username), std::move(secret));
}

void RetroAchievementsSession::enqueueLogin(
    std::string username,
    RaSecretString password
) {
    auto command = std::make_unique<Impl::Command>();
    command->type = Impl::CommandType::PasswordLogin;
    command->username = std::move(username);
    command->secret = std::move(password);
    impl_->enqueue(std::move(command));
}

void RetroAchievementsSession::enqueueTokenLogin(
    std::string username,
    std::string_view token
) {
    RaSecretString secret;
    secret.assign(token);
    enqueueTokenLogin(std::move(username), std::move(secret));
}

void RetroAchievementsSession::enqueueTokenLogin(
    std::string username,
    RaSecretString token
) {
    auto command = std::make_unique<Impl::Command>();
    command->type = Impl::CommandType::TokenLogin;
    command->username = std::move(username);
    command->secret = std::move(token);
    impl_->enqueue(std::move(command));
}

void RetroAchievementsSession::enqueueLogout() {
    auto command = std::make_unique<Impl::Command>();
    command->type = Impl::CommandType::Logout;
    impl_->enqueue(std::move(command));
}

void RetroAchievementsSession::enqueueLoadGame(
    std::uint32_t consoleId,
    std::string romPath
) {
    auto command = std::make_unique<Impl::Command>();
    command->type = Impl::CommandType::LoadGame;
    command->romPath = std::move(romPath);
    command->consoleId = consoleId;
    impl_->enqueue(std::move(command));
}

void RetroAchievementsSession::processPending() {
    impl_->processPending();
}

void RetroAchievementsSession::doFrame() {
    impl_->doFrame();
}

void RetroAchievementsSession::idle() {
    impl_->idle();
}

RaSessionSnapshot RetroAchievementsSession::snapshot() const {
    return impl_->snapshot();
}

std::vector<RaUiEvent> RetroAchievementsSession::takeEvents() {
    return impl_->takeEvents();
}

std::vector<std::uint8_t> RetroAchievementsSession::serializeProgress() const {
    return impl_->serializeProgress();
}

bool RetroAchievementsSession::deserializeProgress(
    std::string_view romHash,
    const std::vector<std::uint8_t>& payload
) {
    return impl_->deserializeProgress(romHash, payload);
}

bool RetroAchievementsSession::resetProgress() {
    return impl_->resetProgress();
}

bool RetroAchievementsSession::shutdown() {
    return impl_->shutdown();
}

std::uint32_t RetroAchievementsSession::readMemoryThunk(
    std::uint32_t address,
    std::uint8_t* buffer,
    std::uint32_t numBytes,
    rc_client_t* client
) {
    RetroAchievementsSession* session = sessionForClient(client);
    if (!session || !session->impl_->onOwnerThread()) {
        return 0;
    }
    return session->impl_->memoryReader
        ? session->impl_->memoryReader(address, buffer, numBytes)
        : 0U;
}

void RetroAchievementsSession::serverCallThunk(
    const rc_api_request_t* request,
    rc_client_server_callback_t callback,
    void* callbackData,
    rc_client_t* client
) {
    RetroAchievementsSession* session = sessionForClient(client);
    if (!session) {
        rc_api_server_response_t error{
            "",
            0,
            RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
        };
        if (callback) {
            callback(&error, callbackData);
        }
        return;
    }
    session->impl_->handleServerCall(request, callback, callbackData);
}

void RetroAchievementsSession::eventThunk(
    const rc_client_event_t* event,
    rc_client_t* client
) {
    RetroAchievementsSession* session = sessionForClient(client);
    if (session && event && session->impl_->onOwnerThread()) {
        session->impl_->handleClientEvent(*event);
    }
}

void RetroAchievementsSession::loginThunk(
    int result,
    const char*,
    rc_client_t*,
    void* callbackData
) {
    auto* context = static_cast<Impl::LoginCallbackContext*>(callbackData);
    if (context && context->session && context->session->impl_->onOwnerThread()) {
        context->session->impl_->loginCompleted(context, result);
    }
}

void RetroAchievementsSession::gameLoadThunk(
    int result,
    const char*,
    rc_client_t*,
    void* callbackData
) {
    auto* context = static_cast<Impl::GameCallbackContext*>(callbackData);
    if (context && context->session && context->session->impl_->onOwnerThread()) {
        context->session->impl_->gameLoadCompleted(context, result);
    }
}

void RetroAchievementsSession::gbProgressThunk(
    int result,
    const char*,
    rc_client_all_user_progress_t* list,
    rc_client_t*,
    void* callbackData
) {
    auto* context = static_cast<Impl::ProfileCallbackContext*>(callbackData);
    if (context && context->session) {
        context->session->impl_->profileCompleted(context, result, list);
    }
}

void RetroAchievementsSession::gbcProgressThunk(
    int result,
    const char*,
    rc_client_all_user_progress_t* list,
    rc_client_t*,
    void* callbackData
) {
    gbProgressThunk(result, nullptr, list, nullptr, callbackData);
}

void RetroAchievementsSession::titlesThunk(
    int result,
    const char*,
    rc_client_game_title_list_t* list,
    rc_client_t*,
    void* callbackData
) {
    auto* context = static_cast<Impl::TitleCallbackContext*>(callbackData);
    if (context && context->session) {
        context->session->impl_->titlesCompleted(context, result, list);
    }
}

} // namespace gb::frontend
