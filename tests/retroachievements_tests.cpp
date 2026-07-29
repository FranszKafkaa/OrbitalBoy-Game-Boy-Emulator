#include "rc_client.h"

#include "test_framework.hpp"

#ifndef GBEMU_ENABLE_RETROACHIEVEMENTS
#error "RetroAchievements tests require GBEMU_ENABLE_RETROACHIEVEMENTS"
#endif

namespace {

uint32_t readMemory(uint32_t, uint8_t*, uint32_t, rc_client_t*) {
    return 0;
}

void callServer(const rc_api_request_t*, rc_client_server_callback_t, void*, rc_client_t*) {
}

} // namespace

TEST_CASE("retroachievements", "client_can_remain_in_casual_mode") {
    rc_client_t* client = rc_client_create(readMemory, callServer);
    T_REQUIRE(client != nullptr);
    rc_client_set_hardcore_enabled(client, 0);
    T_REQUIRE(!rc_client_get_hardcore_enabled(client));
    rc_client_destroy(client);
}
