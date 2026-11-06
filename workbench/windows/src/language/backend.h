#pragma once
#include "service.h"

namespace orbit::language {
// The root/interpreter belong to the backend machine. Leave parentProcessId
// unset for a remote backend, whose process namespace differs from the client.
SessionOptions pyreflySession(std::string rootUri, std::string workspaceName,
                             std::string interpreter,
                             std::optional<std::uint32_t> parentProcessId = {});
} // namespace orbit::language
