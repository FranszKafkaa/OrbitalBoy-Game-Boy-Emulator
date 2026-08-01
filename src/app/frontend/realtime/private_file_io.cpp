#include "gb/app/frontend/realtime/private_file_io.hpp"

#include <atomic>
#include <cerrno>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include <unistd.h>
#endif

namespace gb::frontend::detail {
namespace {

constexpr int kTemporaryFileAttempts = 32;

std::filesystem::path parentDirectory(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

void trace(
    const PrivateFileIoHooks* hooks,
    PrivateFileIoEvent event,
    const std::filesystem::path& path
) {
    if (hooks && hooks->trace) {
        hooks->trace(event, path);
    }
}

std::filesystem::path defaultTemporaryPath(
    const std::filesystem::path& destination
) {
    static std::atomic<std::uint64_t> counter{0};
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::ostringstream suffix;
    suffix << std::hex << generator() << '.'
           << counter.fetch_add(1, std::memory_order_relaxed);
    return parentDirectory(destination)
        / (destination.filename().string() + ".tmp." + suffix.str());
}

std::filesystem::path temporaryPath(
    const std::filesystem::path& destination,
    int attempt,
    const PrivateFileIoHooks* hooks
) {
    return hooks && hooks->chooseTemporaryPath
        ? hooks->chooseTemporaryPath(destination, attempt)
        : defaultTemporaryPath(destination);
}

bool isSibling(
    const std::filesystem::path& path,
    const std::filesystem::path& destination
) {
    return !path.filename().empty()
        && parentDirectory(path).lexically_normal()
            == parentDirectory(destination).lexically_normal();
}

#if defined(_WIN32)

class ScopedWindowsHandle {
public:
    explicit ScopedWindowsHandle(HANDLE handle)
        : handle_(handle) {}

    ScopedWindowsHandle(const ScopedWindowsHandle&) = delete;
    ScopedWindowsHandle& operator=(const ScopedWindowsHandle&) = delete;

    ~ScopedWindowsHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

class CurrentUserOnlySecurityAttributes {
public:
    CurrentUserOnlySecurityAttributes()
        : valid_(initialize()) {}

    [[nodiscard]] const SECURITY_ATTRIBUTES* get() const {
        return valid_ ? &attributes_ : nullptr;
    }

private:
    bool initialize() {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
            return false;
        }
        const ScopedWindowsHandle token(rawToken);
        DWORD tokenUserSize = 0;
        if (GetTokenInformation(token.get(), TokenUser, nullptr, 0, &tokenUserSize)
            || GetLastError() != ERROR_INSUFFICIENT_BUFFER
            || tokenUserSize < sizeof(TOKEN_USER)) {
            return false;
        }
        tokenUser_.resize(tokenUserSize);
        if (!GetTokenInformation(
                token.get(),
                TokenUser,
                tokenUser_.data(),
                tokenUserSize,
                &tokenUserSize
            )) {
            return false;
        }
        const auto* const user =
            reinterpret_cast<const TOKEN_USER*>(tokenUser_.data());
        if (!IsValidSid(user->User.Sid)) {
            return false;
        }
        const DWORD sidSize = GetLengthSid(user->User.Sid);
        constexpr DWORD kAclOverhead =
            sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD);
        if (sidSize == 0U || sidSize > MAXDWORD - kAclOverhead) {
            return false;
        }
        acl_.resize(kAclOverhead + sidSize);
        auto* const dacl = reinterpret_cast<ACL*>(acl_.data());
        if (!InitializeAcl(dacl, static_cast<DWORD>(acl_.size()), ACL_REVISION)
            || !AddAccessAllowedAce(
                dacl,
                ACL_REVISION,
                FILE_ALL_ACCESS,
                user->User.Sid
            )
            || !InitializeSecurityDescriptor(
                &securityDescriptor_,
                SECURITY_DESCRIPTOR_REVISION
            )
            || !SetSecurityDescriptorOwner(
                &securityDescriptor_,
                user->User.Sid,
                FALSE
            )
            || !SetSecurityDescriptorDacl(
                &securityDescriptor_,
                TRUE,
                dacl,
                FALSE
            )
            || !SetSecurityDescriptorControl(
                &securityDescriptor_,
                SE_DACL_PROTECTED,
                SE_DACL_PROTECTED
            )) {
            return false;
        }
        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = &securityDescriptor_;
        attributes_.bInheritHandle = FALSE;
        return true;
    }

    std::vector<BYTE> tokenUser_;
    std::vector<BYTE> acl_;
    SECURITY_DESCRIPTOR securityDescriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
    bool valid_ = false;
};

bool syncDirectory(
    const std::filesystem::path& directory,
    const PrivateFileIoHooks* hooks
) {
    const bool synced = !hooks || !hooks->syncDirectory
        || hooks->syncDirectory(directory);
    trace(
        hooks,
        synced
            ? PrivateFileIoEvent::DirectorySynced
            : PrivateFileIoEvent::DirectorySyncFailed,
        directory
    );
    return synced;
}

bool writeWindows(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size,
    const PrivateFileIoHooks* hooks
) {
    const CurrentUserOnlySecurityAttributes securityAttributes;
    if (!securityAttributes.get()) {
        return false;
    }
    for (int attempt = 0; attempt < kTemporaryFileAttempts; ++attempt) {
        const auto temporary = temporaryPath(destination, attempt, hooks);
        if (!isSibling(temporary, destination)) {
            return false;
        }
        HANDLE handle = CreateFileW(
            temporary.wstring().c_str(),
            GENERIC_WRITE,
            0,
            securityAttributes.get(),
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return false;
        }
        trace(hooks, PrivateFileIoEvent::TemporaryCreated, temporary);
        std::size_t offset = 0;
        bool wrote = true;
        while (offset < size) {
            const std::size_t remaining = size - offset;
            const DWORD chunk = remaining > 0xFFFFFFFFULL
                ? 0xFFFFFFFFU
                : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(
                    handle,
                    contents + offset,
                    chunk,
                    &written,
                    nullptr
                )
                || written == 0U) {
                wrote = false;
                break;
            }
            offset += written;
        }
        const bool flushed = wrote && FlushFileBuffers(handle) != 0;
        if (flushed) {
            trace(hooks, PrivateFileIoEvent::TemporarySynced, temporary);
        }
        const bool closed = CloseHandle(handle) != 0;
        if (!flushed || !closed) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        if (!MoveFileExW(
                temporary.wstring().c_str(),
                destination.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            )) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        trace(hooks, PrivateFileIoEvent::Replaced, destination);
        return syncDirectory(parentDirectory(destination), hooks);
    }
    return false;
}

#else

class ScopedFd {
public:
    explicit ScopedFd(int descriptor = -1)
        : descriptor_(descriptor) {}

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ~ScopedFd() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    [[nodiscard]] int get() const {
        return descriptor_;
    }

private:
    int descriptor_ = -1;
};

int openDirectory(const std::filesystem::path& directory) {
    int flags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    return ::open(directory.c_str(), flags);
}

bool writeAll(
    int descriptor,
    const std::uint8_t* contents,
    std::size_t size
) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(
            descriptor,
            contents + offset,
            size - offset
        );
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool syncDirectory(
    int descriptor,
    const std::filesystem::path& directory,
    const PrivateFileIoHooks* hooks
) {
    const bool synced = hooks && hooks->syncDirectory
        ? hooks->syncDirectory(directory)
        : ::fsync(descriptor) == 0;
    trace(
        hooks,
        synced
            ? PrivateFileIoEvent::DirectorySynced
            : PrivateFileIoEvent::DirectorySyncFailed,
        directory
    );
    return synced;
}

bool renameExclusiveAt(
    int sourceDirectory,
    const char* sourceName,
    int destinationDirectory,
    const char* destinationName
) {
#if defined(__APPLE__)
    return ::renameatx_np(
        sourceDirectory,
        sourceName,
        destinationDirectory,
        destinationName,
        RENAME_EXCL
    ) == 0;
#elif defined(__linux__)
    return ::syscall(
        SYS_renameat2,
        sourceDirectory,
        sourceName,
        destinationDirectory,
        destinationName,
        RENAME_NOREPLACE
    ) == 0;
#else
    static_cast<void>(sourceDirectory);
    static_cast<void>(sourceName);
    static_cast<void>(destinationDirectory);
    static_cast<void>(destinationName);
    errno = ENOTSUP;
    return false;
#endif
}

bool writePosix(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size,
    const PrivateFileIoHooks* hooks
) {
    const auto parent = parentDirectory(destination);
    const ScopedFd directory(openDirectory(parent));
    if (directory.get() < 0) {
        return false;
    }
    for (int attempt = 0; attempt < kTemporaryFileAttempts; ++attempt) {
        const auto temporary = temporaryPath(destination, attempt, hooks);
        if (!isSibling(temporary, destination)) {
            return false;
        }
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
        flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        const int descriptor = ::openat(
            directory.get(),
            temporary.filename().c_str(),
            flags,
            S_IRUSR | S_IWUSR
        );
        if (descriptor < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return false;
        }
        trace(hooks, PrivateFileIoEvent::TemporaryCreated, temporary);
        const bool wrote = writeAll(descriptor, contents, size);
        const bool flushed = wrote && ::fsync(descriptor) == 0;
        if (flushed) {
            trace(hooks, PrivateFileIoEvent::TemporarySynced, temporary);
        }
        const bool closed = ::close(descriptor) == 0;
        if (!flushed || !closed) {
            ::unlinkat(
                directory.get(),
                temporary.filename().c_str(),
                0
            );
            return false;
        }
        if (::renameat(
                directory.get(),
                temporary.filename().c_str(),
                directory.get(),
                destination.filename().c_str()
            ) != 0) {
            ::unlinkat(
                directory.get(),
                temporary.filename().c_str(),
                0
            );
            return false;
        }
        trace(hooks, PrivateFileIoEvent::Replaced, destination);
        return syncDirectory(directory.get(), parent, hooks);
    }
    return false;
}

#endif

} // namespace

bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size,
    const PrivateFileIoHooks* hooks
) {
    if (destination.empty() || destination.filename().empty()
        || (contents == nullptr && size != 0U)) {
        return false;
    }
    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }
#if defined(_WIN32)
    return writeWindows(destination, contents, size, hooks);
#else
    return writePosix(destination, contents, size, hooks);
#endif
}

bool removeFileDurably(
    const std::filesystem::path& path,
    bool* entryChanged,
    const PrivateFileIoHooks* hooks
) {
    if (entryChanged) {
        *entryChanged = false;
    }
    if (path.empty() || path.filename().empty()) {
        return false;
    }
    const auto parent = parentDirectory(path);
#if defined(_WIN32)
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) {
        return false;
    }
    if (!removed) {
        return true;
    }
    if (entryChanged) {
        *entryChanged = true;
    }
    trace(hooks, PrivateFileIoEvent::Removed, path);
    return syncDirectory(parent, hooks);
#else
    const ScopedFd directory(openDirectory(parent));
    if (directory.get() < 0) {
        return false;
    }
    if (::unlinkat(directory.get(), path.filename().c_str(), 0) != 0) {
        return errno == ENOENT;
    }
    if (entryChanged) {
        *entryChanged = true;
    }
    trace(hooks, PrivateFileIoEvent::Removed, path);
    return syncDirectory(directory.get(), parent, hooks);
#endif
}

bool renameFileDurably(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    bool* entryChanged,
    const PrivateFileIoHooks* hooks
) {
    if (entryChanged) {
        *entryChanged = false;
    }
    if (source.empty() || destination.empty()
        || source.filename().empty() || destination.filename().empty()) {
        return false;
    }
    const auto sourceParent = parentDirectory(source);
    const auto destinationParent = parentDirectory(destination);
#if defined(_WIN32)
    if (!MoveFileExW(
            source.wstring().c_str(),
            destination.wstring().c_str(),
            MOVEFILE_WRITE_THROUGH
        )) {
        return false;
    }
    if (entryChanged) {
        *entryChanged = true;
    }
    trace(hooks, PrivateFileIoEvent::Renamed, destination);
    const bool sourceSynced = syncDirectory(sourceParent, hooks);
    const bool destinationSynced =
        sourceParent.lexically_normal() == destinationParent.lexically_normal()
            || syncDirectory(destinationParent, hooks);
    return sourceSynced && destinationSynced;
#else
    const ScopedFd sourceDirectory(openDirectory(sourceParent));
    if (sourceDirectory.get() < 0) {
        return false;
    }
    const bool sameParent =
        sourceParent.lexically_normal() == destinationParent.lexically_normal();
    const ScopedFd destinationDirectory(
        sameParent ? -1 : openDirectory(destinationParent)
    );
    if (!sameParent && destinationDirectory.get() < 0) {
        return false;
    }
    const int destinationDescriptor = sameParent
        ? sourceDirectory.get()
        : destinationDirectory.get();
    if (!renameExclusiveAt(
            sourceDirectory.get(),
            source.filename().c_str(),
            destinationDescriptor,
            destination.filename().c_str()
        )) {
        return false;
    }
    if (entryChanged) {
        *entryChanged = true;
    }
    trace(hooks, PrivateFileIoEvent::Renamed, destination);
    const bool sourceSynced =
        syncDirectory(sourceDirectory.get(), sourceParent, hooks);
    const bool destinationSynced = sameParent
        || syncDirectory(
            destinationDirectory.get(),
            destinationParent,
            hooks
        );
    return sourceSynced && destinationSynced;
#endif
}

bool makeFileOwnerPrivate(const std::filesystem::path& path) {
#if defined(_WIN32)
    const CurrentUserOnlySecurityAttributes securityAttributes;
    const auto* attributes = securityAttributes.get();
    if (!attributes || !attributes->lpSecurityDescriptor) {
        return false;
    }
    return SetFileSecurityW(
        path.wstring().c_str(),
        OWNER_SECURITY_INFORMATION
            | DACL_SECURITY_INFORMATION
            | PROTECTED_DACL_SECURITY_INFORMATION,
        static_cast<PSECURITY_DESCRIPTOR>(attributes->lpSecurityDescriptor)
    ) != 0;
#else
    if (path.empty() || path.filename().empty()) {
        return false;
    }
    const ScopedFd directory(openDirectory(parentDirectory(path)));
    if (directory.get() < 0) {
        return false;
    }
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const ScopedFd file(::openat(directory.get(), path.filename().c_str(), flags));
    if (file.get() < 0) {
        return false;
    }
    struct stat status {};
    return ::fstat(file.get(), &status) == 0
        && S_ISREG(status.st_mode)
        && ::fchmod(file.get(), S_IRUSR | S_IWUSR) == 0;
#endif
}

} // namespace gb::frontend::detail
