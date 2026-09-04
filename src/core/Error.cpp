#include "core/Error.h"

#include <cerrno>

namespace uta {

ErrorCode errorCodeFromErrno(int value) noexcept {
    switch (value) {
    case EACCES:
    case EPERM:  return ErrorCode::PermissionDenied;
    case ENOENT: return ErrorCode::NotFound;
    case EEXIST: return ErrorCode::AlreadyExists;
    case EISDIR: return ErrorCode::InvalidArgument;
    case ENOMEM: return ErrorCode::OutOfMemory;
    default:     return ErrorCode::IoFailure;
    }
}

std::string_view errorCodeName(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Unknown:            return "Unknown";
    case ErrorCode::NotFound:           return "NotFound";
    case ErrorCode::PermissionDenied:   return "PermissionDenied";
    case ErrorCode::AlreadyExists:      return "AlreadyExists";
    case ErrorCode::InvalidArgument:    return "InvalidArgument";
    case ErrorCode::MalformedData:      return "MalformedData";
    case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
    case ErrorCode::IoFailure:          return "IoFailure";
    case ErrorCode::OutOfMemory:        return "OutOfMemory";
    case ErrorCode::Cancelled:          return "Cancelled";
    }
    // A value outside the enumeration -- reachable only by a cast, and named
    // rather than left to fall off the end of the function.
    return "Unknown";
}

Error Error::withContext(std::string_view context) const {
    std::string prefixed;
    prefixed.reserve(context.size() + 2 + message_.size());
    prefixed.append(context);
    prefixed.append(": ");
    prefixed.append(message_);
    return Error(code_, std::move(prefixed));
}

}  // namespace uta
