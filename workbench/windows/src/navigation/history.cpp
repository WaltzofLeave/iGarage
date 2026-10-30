#include "history.h"

#include <algorithm>
#include <utility>

namespace orbit {

void NavigationHistory::visit(NavigationLocation location) {
    if (location.path.empty()) return;
    touchRecent(location.path);
    if (!locations_.empty()) {
        auto& current = locations_[cursor_];
        if (current.path == location.path && current.view.caret == location.view.caret) {
            current = std::move(location);
            return;
        }
        locations_.erase(locations_.begin() + cursor_ + 1, locations_.end());
    }
    locations_.push_back(std::move(location));
    if (locations_.size() > historyLimit) locations_.erase(locations_.begin());
    cursor_ = locations_.size() - 1;
}

void NavigationHistory::depart(const NavigationLocation& location) {
    if (location.path.empty()) return;
    if (locations_.empty() || locations_[cursor_].path != location.path) {
        visit(location);
    } else {
        locations_[cursor_] = location;
    }
}

std::optional<NavigationLocation> NavigationHistory::back(const NavigationLocation& current) {
    depart(current);
    if (!canGoBack()) return std::nullopt;
    return locations_[--cursor_];
}

std::optional<NavigationLocation> NavigationHistory::forward(const NavigationLocation& current) {
    depart(current);
    if (!canGoForward()) return std::nullopt;
    return locations_[++cursor_];
}

bool NavigationHistory::canGoBack() const {
    return !locations_.empty() && cursor_ > 0;
}

bool NavigationHistory::canGoForward() const {
    return !locations_.empty() && cursor_ + 1 < locations_.size();
}

void NavigationHistory::touchRecent(const std::string& path) {
    if (path.empty()) return;
    std::erase(recent_, path);
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > recentLimit) recent_.resize(recentLimit);
}

void NavigationHistory::restoreRecent(const std::vector<std::string>& paths) {
    recent_.clear();
    for (const auto& path : paths) {
        if (!path.empty() && std::find(recent_.begin(), recent_.end(), path) == recent_.end()) {
            recent_.push_back(path);
            if (recent_.size() == recentLimit) break;
        }
    }
}

void NavigationHistory::rememberClosed(NavigationLocation location) {
    if (location.path.empty()) return;
    std::erase_if(closed_, [&](const auto& item) { return item.path == location.path; });
    closed_.insert(closed_.begin(), std::move(location));
    if (closed_.size() > closedLimit) closed_.resize(closedLimit);
}

std::optional<NavigationLocation> NavigationHistory::takeClosed() {
    if (closed_.empty()) return std::nullopt;
    auto location = std::move(closed_.front());
    closed_.erase(closed_.begin());
    return location;
}

void NavigationHistory::renamePath(const std::string& oldPath, const std::string& newPath) {
    for (auto& item : locations_) if (item.path == oldPath) item.path = newPath;
    for (auto& item : closed_) if (item.path == oldPath) item.path = newPath;
    for (auto& path : recent_) if (path == oldPath) path = newPath;
}

void NavigationHistory::forgetPath(const std::string& path) {
    std::size_t removedBeforeCursor = 0;
    for (std::size_t i = 0; i < locations_.size() && i < cursor_; ++i) {
        if (locations_[i].path == path) ++removedBeforeCursor;
    }
    std::erase_if(locations_, [&](const auto& item) { return item.path == path; });
    cursor_ = locations_.empty() ? 0 : std::min(cursor_ - removedBeforeCursor, locations_.size() - 1);
    std::erase(recent_, path);
    std::erase_if(closed_, [&](const auto& item) { return item.path == path; });
}

void NavigationHistory::resetHistory() {
    locations_.clear();
    closed_.clear();
    cursor_ = 0;
}

void NavigationHistory::clear() {
    resetHistory();
    recent_.clear();
}

} // namespace orbit
