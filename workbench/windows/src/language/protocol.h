#pragma once

#include "../../vendor/json.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orbit::language {
using Json = nlohmann::json;

struct Position {
    int line = 0, character = 0;
    bool operator==(const Position&) const = default;
};
struct Range { Position start, end; };
Json encode(Position position);
Position position(const Json& value);
Range range(const Json& value);

// Scintilla uses UTF-8 byte offsets. The negotiated LSP encoding is UTF-16.
class TextBuffer {
public:
    TextBuffer() = default;
    explicit TextBuffer(std::string text);
    const std::string& text() const { return text_; }
    Position at(std::size_t byte) const;
    std::optional<std::size_t> offset(Position position) const;
    Json change(std::size_t byte, std::size_t removed, std::string_view inserted);
private:
    void index();
    std::string text_;
    std::vector<std::size_t> lines_{0};
};

class FrameDecoder {
public:
    static constexpr std::size_t maxBody = 16 * 1024 * 1024;
    std::vector<Json> feed(std::string_view bytes);
private:
    std::string buffer_;
    std::optional<std::size_t> expected_;
};
std::string frame(const Json& message);
std::string fileUri(std::string_view absolutePath);
std::optional<std::string> uriPath(std::string_view uri);
std::string plainDocumentation(const Json& value, std::size_t limit = 6000);

struct Completion {
    std::string label, detail, documentation, filter, sort;
    int kind = 1;
    bool resolved = false;
    Json item;
};
std::vector<Completion> completions(const Json& result);
struct Replacement { std::size_t start = 0, end = 0; std::string text; };
struct EditPlan {
    std::vector<Replacement> edits; // descending byte offsets, validated before mutation
    std::size_t caret = 0;
};
std::optional<EditPlan> completionEdit(const TextBuffer& buffer, const Completion& completion,
                                       std::size_t prefixStart, std::size_t caret);
struct Signature { std::string text; int highlightStart = 0, highlightEnd = 0; };
Signature signature(const Json& result);
// Wrap native calltips without splitting UTF-8 or moving the active-parameter span.
Signature boundedTip(Signature value);
struct Diagnostic {
    Range span;
    std::string message, source;
    int severity = 1;
};
std::vector<Diagnostic> diagnostics(const Json& values);

} // namespace orbit::language
