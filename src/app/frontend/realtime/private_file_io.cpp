#include "private_file_io.hpp"

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
#include <unistd.h>
#endif

namespace gb::frontend::detail {
namespace {

constexpr int kTemporaryFileAttempts = 32;

enum class TemporaryWriteResult {
    Success,
    Collision,
    Failed,
};

std::filesystem::path temporarySiblingPath(
    const std::filesystem::path& destination
) {
    static std::atomic<std::uint64_t> counter{0};
    thread_local std::mt19937_64 generator(std::random_device{}());
    std::ostringstream suffix;
    suffix << std::hex << generator() << '.'
           << counter.fetch_add(1, std::memory_order_relaxed);
    return destination.parent_path()
        / (destination.filename().string() + ".tmp." + suffix.str());
}

void removeTemporaryFile(const std::filesystem::path& temporary) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
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

TemporaryWriteResult writeExclusivePrivateTemporary(
    const std::filesystem::path& path,
    const std::uint8_t* contents,
    std::size_t size
) {
    const CurrentUserOnlySecurityAttributes securityAttributes;
    if (securityAttributes.get() == nullptr) {
        return TemporaryWriteResult::Failed;
    }
    HANDLE handle = CreateFileW(
        path.wstring().c_str(),
        GENERIC_WRITE,
        0,
        securityAttributes.get(),
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
            ? TemporaryWriteResult::Collision
            : TemporaryWriteResult::Failed;
    }
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
    const bool closed = CloseHandle(handle) != 0;
    return flushed && closed
        ? TemporaryWriteResult::Success
        : TemporaryWriteResult::Failed;
}

bool replaceWithTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
    return MoveFileExW(
        temporary.wstring().c_str(),
        destination.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
}

#else

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

TemporaryWriteResult writeExclusivePrivateTemporary(
    const std::filesystem::path& path,
    const std::uint8_t* contents,
    std::size_t size
) {
    const int descriptor = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_EXCL
#if defined(O_CLOEXEC)
            | O_CLOEXEC
#endif
        ,
        S_IRUSR | S_IWUSR
    );
    if (descriptor < 0) {
        return errno == EEXIST
            ? TemporaryWriteResult::Collision
            : TemporaryWriteResult::Failed;
    }
    const bool wrote = writeAll(descriptor, contents, size);
    const bool flushed = wrote && ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    return flushed && closed
        ? TemporaryWriteResult::Success
        : TemporaryWriteResult::Failed;
}

bool replaceWithTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
    return ::rename(temporary.c_str(), destination.c_str()) == 0;
}

#endif

} // namespace

bool writePrivateFileAtomically(
    const std::filesystem::path& destination,
    const std::uint8_t* contents,
    std::size_t size
) {
    if (destination.empty() || (contents == nullptr && size != 0U)) {
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
    for (int attempt = 0; attempt < kTemporaryFileAttempts; ++attempt) {
        const auto temporary = temporarySiblingPath(destination);
        const auto writeResult = writeExclusivePrivateTemporary(
            temporary,
            contents,
            size
        );
        if (writeResult != TemporaryWriteResult::Success) {
            if (writeResult == TemporaryWriteResult::Collision) {
                continue;
            }
            removeTemporaryFile(temporary);
            return false;
        }
        if (replaceWithTemporaryFile(temporary, destination)) {
            return true;
        }
        removeTemporaryFile(temporary);
        return false;
    }
    return false;
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
    return ::chmod(path.c_str(), S_IRUSR | S_IWUSR) == 0;
#endif
}

} // namespace gb::frontend::detail
