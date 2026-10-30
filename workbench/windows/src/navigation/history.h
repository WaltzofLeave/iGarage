#pragma once

#include "../editor/view_state.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace orbit {

struct NavigationLocation {
    std::string path;
    EditorViewState view;
    int tabIndex = -1;
};

// UI-independent, bounded state. No document contents or native handles live here.
class NavigationHistory {
public:
    static constexpr std::size_t historyLimit = 128;
    static constexpr std::size_t recentLimit = 64;
    static constexpr std::size_t closedLimit = 20;

    void visit(NavigationLocation location);
    void depart(const NavigationLocation& location);
    std::optional<NavigationLocation> back(const NavigationLocation& current);
    std::optional<NavigationLocation> forward(const NavigationLocation& current);
    bool canGoBack() const;
    bool canGoForward() const;

    void touchRecent(const std::string& path);
    void restoreRecent(const std::vector<std::string>& paths);
    const std::vector<std::string>& recent() const { return recent_; }

    void rememberClosed(NavigationLocation location);
    std::optional<NavigationLocation> takeClosed();
    void renamePath(const std::string& oldPath, const std::string& newPath);
    void forgetPath(const std::string& path);
    void resetHistory();
    void clear();

    std::size_t size() const { return locations_.size(); }

private:
    std::vector<NavigationLocation> locations_;
    std::vector<std::string> recent_;
    std::vector<NavigationLocation> closed_;
    std::size_t cursor_ = 0;
};

} // namespace orbit
