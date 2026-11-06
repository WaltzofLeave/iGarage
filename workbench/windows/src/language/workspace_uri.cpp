#include "workspace_uri.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace orbit::language {
namespace {
std::string normalized(std::string path, bool insensitive) {
    for (auto& c : path) {
        if (c == '\\') c = '/';
        if (insensitive && static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return path;
}
bool drivePath(std::string_view path) {
    return path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
}
} // namespace
WorkspaceUris::WorkspaceUris(std::string rootUri) {
    const auto decoded = uriPath(rootUri);
    if (!decoded || decoded->empty()) throw std::invalid_argument("Workspace requires an absolute file URI");
    rootPath_ = *decoded;
    caseInsensitive_ = drivePath(rootPath_) || rootPath_.starts_with("\\\\");
    if (!caseInsensitive_ && rootPath_.find('\\') != rootPath_.npos)
        throw std::invalid_argument("POSIX workspace root contains an unsupported backslash filename");
    const auto minimum = drivePath(rootPath_) ? 3u : 1u;
    while (rootPath_.size() > minimum && (rootPath_.back() == '/' || rootPath_.back() == '\\')) rootPath_.pop_back();
    normalizedRoot_ = normalized(rootPath_, caseInsensitive_);
    if (!normalizedRoot_.ends_with('/')) normalizedRoot_ += '/';
    rootUri_ = fileUri(rootPath_);
}
bool WorkspaceUris::validRelative(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find_first_of("\\:") != path.npos || path.find('\0') != path.npos) return false;
    for (std::size_t start = 0; start < path.size();) {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end == path.npos ? path.size() - start : end - start);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == path.npos) return true;
        start = end + 1;
    }
    return false;
}
std::string WorkspaceUris::document(std::string_view relativePath) const {
    if (rootPath_.empty() || !validRelative(relativePath)) throw std::invalid_argument("Invalid workspace-relative document path");
    auto path = rootPath_;
    if (path.back() != '/' && path.back() != '\\') path += '/';
    path += relativePath;
    return fileUri(path);
}
std::optional<std::string> WorkspaceUris::relative(std::string_view documentUri) const {
    const auto decoded = uriPath(documentUri);
    if (!decoded || normalizedRoot_.empty()) return {};
    if (!caseInsensitive_ && decoded->find('\\') != decoded->npos) return {};
    const auto candidate = normalized(*decoded, caseInsensitive_);
    if (!candidate.starts_with(normalizedRoot_)) return {};
    auto relativePath = normalized(decoded->substr(normalizedRoot_.size()), false);
    if (!validRelative(relativePath)) return {};
    return relativePath;
}
} // namespace orbit::language
