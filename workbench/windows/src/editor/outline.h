#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

struct Symbol {
    std::string name;
    int line = 0;
    int depth = 0;
    char kind = 'f';
};

namespace orbit {

struct DocumentOutline {
    std::vector<Symbol> symbols;
    std::set<std::string> completionWords;
};

// A lexical outline, not a language server. Callers enforce the document-size budget.
DocumentOutline analyzeDocument(std::string_view text, std::string_view extension);

} // namespace orbit
