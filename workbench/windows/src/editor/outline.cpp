#include "outline.h"

#include <algorithm>
#include <cctype>

namespace orbit {
namespace {

bool identifier(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

std::string_view strip(std::string_view text) {
    const auto start = text.find_first_not_of(" \t\r");
    return start == text.npos ? std::string_view{} : text.substr(start);
}

void addSymbol(DocumentOutline& result, std::string_view text, std::string_view prefix,
               int line, int depth, char kind) {
    if (!text.starts_with(prefix)) return;
    text.remove_prefix(prefix.size());
    std::size_t length = 0;
    while (length < text.size() && identifier(static_cast<unsigned char>(text[length]))) ++length;
    if (length) result.symbols.push_back({std::string(text.substr(0, length)), line, depth, kind});
}

} // namespace

DocumentOutline analyzeDocument(std::string_view text, std::string_view language) {
    DocumentOutline result;
    int lineNumber = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        const auto content = strip(line);
        const int depth = std::min(2, static_cast<int>(line.size() - content.size()) / 4);

        if (language == "py") {
            addSymbol(result, content, "class ", lineNumber, depth, 'C');
            addSymbol(result, content, "def ", lineNumber, depth, 'f');
            addSymbol(result, content, "async def ", lineNumber, depth, 'f');
        } else if (language == "md" && content.starts_with('#')) {
            const auto heading = content.find_first_not_of('#');
            if (heading != content.npos) {
                auto title = strip(content.substr(heading));
                if (!title.empty()) result.symbols.push_back({std::string(title), lineNumber,
                    std::min(2, static_cast<int>(heading) - 1), '#'});
            }
        } else {
            auto declaration = content;
            if (declaration.starts_with("export ")) declaration.remove_prefix(7);
            if (declaration.starts_with("default ")) declaration.remove_prefix(8);
            if (declaration.starts_with("async ")) declaration.remove_prefix(6);
            addSymbol(result, declaration, "class ", lineNumber, depth, 'C');
            addSymbol(result, declaration, "function ", lineNumber, depth, 'f');
            addSymbol(result, declaration, "const ", lineNumber, depth, '#');
            addSymbol(result, declaration, "let ", lineNumber, depth, '#');
        }

        std::size_t tokenStart = 0;
        while (tokenStart < line.size()) {
            while (tokenStart < line.size() && !identifier(static_cast<unsigned char>(line[tokenStart]))) ++tokenStart;
            auto tokenEnd = tokenStart;
            while (tokenEnd < line.size() && identifier(static_cast<unsigned char>(line[tokenEnd]))) ++tokenEnd;
            const auto length = tokenEnd - tokenStart;
            if (length >= 3 && length < 80) result.completionWords.emplace(line.substr(tokenStart, length));
            tokenStart = tokenEnd;
        }

        ++lineNumber;
        if (end == text.npos) break;
        text.remove_prefix(end + 1);
    }
    return result;
}

} // namespace orbit
