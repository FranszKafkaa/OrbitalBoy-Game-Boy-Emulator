#include <type_traits>

#include "gb/achievements/security/secret_string.hpp"
#include "gb/app/frontend/realtime/secure_string.hpp"

static_assert(std::is_same_v<
    gb::frontend::SecureStringWipeObserver,
    gb::achievements::SecretWipeObserver
>);
static_assert(std::is_same_v<
    gb::frontend::RaSecretString,
    gb::achievements::SecretString
>);
