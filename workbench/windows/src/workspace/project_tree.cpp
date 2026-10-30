#include "project_tree.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace orbit {
namespace {
std::string folded(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
} // namespace

void ProjectTreeIndex::rebuild(const std::vector<std::string>& paths) {
    children_.clear();
    pathCount_ = paths.size();
    std::set<std::string> folders;
    for (const auto& path : paths) {
        std::string parent;
        std::size_t start = 0;
        for (;;) {
            const auto slash = path.find('/', start);
            const bool folder = slash != path.npos;
            const auto childPath = folder ? path.substr(0, slash) : path;
            if (!folder || folders.insert(childPath).second) {
                children_[parent].push_back({childPath, path.substr(start, folder ? slash - start : path.size() - start), 0, folder});
            }
            if (!folder) break;
            parent = childPath;
            start = slash + 1;
        }
    }
    for (auto& [parent, rows] : children_) {
        std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
            if (left.folder != right.folder) return left.folder;
            const auto a = folded(left.label), b = folded(right.label);
            return a == b ? left.path < right.path : a < b;
        });
    }
}

std::vector<TreeRow> ProjectTreeIndex::visibleRows(const std::set<std::string>& expanded,
                                                  std::string rootLabel) const {
    std::vector<TreeRow> result;
    result.reserve(pathCount_ + children_.size() + 1);
    result.push_back({"", std::move(rootLabel), 0, true});
    const std::function<void(const std::string&, int)> append = [&](const auto& directory, int depth) {
        if (!expanded.contains(directory)) return;
        const auto found = children_.find(directory);
        if (found == children_.end()) return;
        for (const auto& entry : found->second) {
            auto row = entry;
            row.depth = depth;
            result.push_back(std::move(row));
            if (entry.folder) append(entry.path, depth + 1);
        }
    };
    append("", 1);
    return result;
}

} // namespace orbit
