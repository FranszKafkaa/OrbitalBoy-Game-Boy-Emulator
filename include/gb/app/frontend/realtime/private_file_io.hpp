#pragma once

#include "gb/achievements/storage/private_file_io.hpp"

namespace gb::frontend::detail {

using PrivateFileIoEvent = gb::achievements::storage::PrivateFileIoEvent;
using PrivateFileIoHooks = gb::achievements::storage::PrivateFileIoHooks;
using gb::achievements::storage::makeFileOwnerPrivate;
using gb::achievements::storage::removeFileDurably;
using gb::achievements::storage::renameFileDurably;
using gb::achievements::storage::writePrivateFileAtomically;

} // namespace gb::frontend::detail
