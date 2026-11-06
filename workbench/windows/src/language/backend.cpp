#include "backend.h"
#include <utility>

namespace orbit::language {
SessionOptions pyreflySession(std::string rootUri, std::string workspaceName,
                             std::string interpreter, std::optional<std::uint32_t> parentProcessId) {
    SessionOptions session;
    session.rootUri = std::move(rootUri);
    session.workspaceName = std::move(workspaceName);
    session.languageId = "python";
    session.serverName = "Pyrefly";
    session.parentProcessId = parentProcessId;
    session.initializationOptions = {{"pythonPath", std::move(interpreter)},
        {"analysis", {{"diagnosticMode", "openFilesOnly"}, {"autoImportCompletions", true}, {"completeFunctionParens", false}}},
        {"pyrefly", {{"typeCheckingMode", "basic"}, {"diagnosticMode", "openFilesOnly"}, {"streamDiagnostics", true}}}};
    session.settings = {{"python", session.initializationOptions}};
    return session;
}
} // namespace orbit::language
