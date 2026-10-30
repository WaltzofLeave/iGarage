#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

struct TreeRow {
    std::string path;
    std::string label;
    int depth = 0;
    bool folder = false;
};

namespace orbit {

// Build directory membership once; expanding a directory never rescans all paths.
class ProjectTreeIndex {
public:
    void rebuild(const std::vector<std::string>& paths);
    std::vector<TreeRow> visibleRows(const std::set<std::string>& expanded,
                                     std::string rootLabel) const;
    std::size_t pathCount() const { return pathCount_; }

private:
    std::map<std::string, std::vector<TreeRow>> children_;
    std::size_t pathCount_ = 0;
};

} // namespace orbit
