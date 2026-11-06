#pragma once
#include "protocol.h"

namespace orbit::language {
// Maps frontend relative names to backend-owned file URIs without consulting
// the local filesystem. Windows and POSIX backend roots are both supported.
class WorkspaceUris {
public:
    WorkspaceUris() = default;
    explicit WorkspaceUris(std::string rootUri);
    const std::string& root() const { return rootUri_; }
    std::string document(std::string_view relativePath) const;
    std::optional<std::string> relative(std::string_view documentUri) const;
    static bool validRelative(std::string_view path);
private:
    std::string rootUri_, rootPath_, normalizedRoot_;
    bool caseInsensitive_ = false;
};
} // namespace orbit::language
