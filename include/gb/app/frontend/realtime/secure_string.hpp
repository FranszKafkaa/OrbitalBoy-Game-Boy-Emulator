#pragma once

#include "gb/achievements/security/secret_string.hpp"

namespace gb::frontend {

using SecureStringWipeObserver = gb::achievements::SecretWipeObserver;
using RaSecretString = gb::achievements::SecretString;
using gb::achievements::secureEraseStringStorage;

} // namespace gb::frontend
