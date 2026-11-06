#include "protocol.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>

namespace orbit::language {
namespace {
std::pair<std::size_t, int> codepoint(std::string_view text, std::size_t at) {
    const auto c = static_cast<unsigned char>(text[at]);
    const std::size_t length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    if (at + length > text.size()) throw std::runtime_error("Incomplete UTF-8 position");
    for (std::size_t i = 1; i < length; ++i)
        if ((static_cast<unsigned char>(text[at + i]) & 0xc0) != 0x80) throw std::runtime_error("Invalid UTF-8 position");
    return {length, length == 4 ? 2 : 1};
}
std::string lowerAscii(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
std::string snippetText(std::string_view input) {
    // The client advertises plain text only. Defensive support for simple server placeholders.
    std::string output;
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] == '\\' && i + 1 < input.size()) { output += input[i + 1]; i += 2; continue; }
        if (input[i] != '$') { output += input[i++]; continue; }
        ++i;
        if (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
            while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) ++i;
        } else if (i < input.size() && input[i] == '{') {
            const auto end = input.find('}', ++i);
            if (end == input.npos) throw std::runtime_error("Invalid completion placeholder");
            const auto colon = input.find(':', i);
            if (colon < end) output += input.substr(colon + 1, end - colon - 1);
            i = end + 1;
        } else throw std::runtime_error("Unsupported completion variable");
    }
    return output;
}
} // namespace

Json encode(Position p) { return {{"line", p.line}, {"character", p.character}}; }
Position position(const Json& value) { return {value.at("line").get<int>(), value.at("character").get<int>()}; }
Range range(const Json& value) { return {position(value.at("start")), position(value.at("end"))}; }

TextBuffer::TextBuffer(std::string text) : text_(std::move(text)) { index(); }
void TextBuffer::index() {
    lines_ = {0};
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\r') { if (i + 1 < text_.size() && text_[i + 1] == '\n') ++i; lines_.push_back(i + 1); }
        else if (text_[i] == '\n') lines_.push_back(i + 1);
    }
}
Position TextBuffer::at(std::size_t byte) const {
    if (byte > text_.size()) throw std::runtime_error("Position outside document");
    const auto line = static_cast<std::size_t>(std::upper_bound(lines_.begin(), lines_.end(), byte) - lines_.begin() - 1);
    int units = 0;
    for (std::size_t i = lines_[line]; i < byte;) {
        const auto [length, width] = codepoint(text_, i);
        if (i + length > byte) throw std::runtime_error("Position splits UTF-8 character");
        if (text_[i] == '\r' || text_[i] == '\n') break;
        units += width; i += length;
    }
    return {static_cast<int>(line), units};
}
std::optional<std::size_t> TextBuffer::offset(Position p) const {
    if (p.line < 0 || p.character < 0 || static_cast<std::size_t>(p.line) >= lines_.size()) return {};
    std::size_t i = lines_[p.line];
    int units = 0;
    while (i < text_.size() && text_[i] != '\r' && text_[i] != '\n' && units < p.character) {
        const auto [length, width] = codepoint(text_, i);
        units += width; i += length;
    }
    return units == p.character ? std::optional{i} : std::nullopt;
}
Json TextBuffer::change(std::size_t byte, std::size_t removed, std::string_view inserted) {
    if (byte > text_.size() || removed > text_.size() - byte) throw std::runtime_error("Invalid document change");
    const auto start = at(byte), end = at(byte + removed);
    const bool newline = inserted.find_first_of("\r\n") != inserted.npos ||
        std::string_view(text_).substr(byte, removed).find_first_of("\r\n") != std::string_view::npos;
    const auto delta = static_cast<std::ptrdiff_t>(inserted.size()) - static_cast<std::ptrdiff_t>(removed);
    text_.replace(byte, removed, inserted);
    if (newline) index();
    else for (auto it = std::upper_bound(lines_.begin(), lines_.end(), byte); it != lines_.end(); ++it)
        *it = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*it) + delta);
    return {{"range", {{"start", encode(start)}, {"end", encode(end)}}}, {"text", inserted}};
}

std::vector<Json> FrameDecoder::feed(std::string_view bytes) {
    buffer_.append(bytes);
    std::vector<Json> messages;
    for (;;) {
        if (!expected_) {
            const auto end = buffer_.find("\r\n\r\n");
            if (end == buffer_.npos) { if (buffer_.size() > 8192) throw std::runtime_error("Oversized LSP header"); break; }
            if (end > 8192) throw std::runtime_error("Oversized LSP header");
            std::optional<std::size_t> size;
            for (std::size_t pos = 0; pos < end;) {
                const auto lineEnd = buffer_.find("\r\n", pos);
                const auto line = std::string_view(buffer_).substr(pos, lineEnd - pos);
                const auto colon = line.find(':');
                if (colon == line.npos) throw std::runtime_error("Malformed LSP header");
                if (lowerAscii(std::string(line.substr(0, colon))) == "content-length") {
                    if (size) throw std::runtime_error("Duplicate Content-Length");
                    auto number = line.substr(colon + 1);
                    while (!number.empty() && number.front() == ' ') number.remove_prefix(1);
                    std::size_t parsed = 0;
                    const auto conversion = std::from_chars(number.data(), number.data() + number.size(), parsed);
                    if (conversion.ec != std::errc{} || conversion.ptr != number.data() + number.size() || !parsed || parsed > maxBody)
                        throw std::runtime_error("Invalid Content-Length");
                    size = parsed;
                }
                pos = lineEnd + 2;
            }
            if (!size) throw std::runtime_error("Missing Content-Length");
            expected_ = *size;
            buffer_.erase(0, end + 4);
        }
        if (buffer_.size() < *expected_) break;
        messages.push_back(Json::parse(buffer_.begin(), buffer_.begin() + *expected_));
        buffer_.erase(0, *expected_);
        expected_.reset();
    }
    if (buffer_.size() > maxBody) throw std::runtime_error("Oversized LSP message");
    return messages;
}
std::string frame(const Json& message) {
    auto body = message.dump();
    if (body.size() > FrameDecoder::maxBody) throw std::runtime_error("Oversized LSP request");
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
std::string fileUri(std::string_view path) {
    std::string uri = path.starts_with('/') ? "file://" : "file:///";
    if (path.starts_with("\\\\")) { uri = "file://"; path.remove_prefix(2); }
    constexpr char digits[] = "0123456789ABCDEF";
    for (const unsigned char c : path) {
        if (c == '\\') uri += '/';
        else if ((std::isalnum(c) && c < 128) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':' || c == '/') uri += c;
        else { uri += '%'; uri += digits[c >> 4]; uri += digits[c & 15]; }
    }
    return uri;
}
std::optional<std::string> uriPath(std::string_view uri) {
    if (!uri.starts_with("file://")) return {};
    uri.remove_prefix(7);
    if (uri.empty() || uri.find_first_of("?#") != uri.npos) return {};
    std::string result;
    const bool windowsDrive = uri.size() >= 4 && uri[0] == '/' && std::isalpha(static_cast<unsigned char>(uri[1])) && uri[2] == ':' && uri[3] == '/';
    const bool windowsPath = !uri.starts_with('/') || windowsDrive;
    if (!uri.starts_with('/')) result = "\\\\";
    else if (windowsDrive) uri.remove_prefix(1);
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] == '%') {
            if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0) return {};
            const char c = static_cast<char>((hex(uri[i + 1]) << 4) | hex(uri[i + 2]));
            if (!c) return {};
            result += c; i += 2;
        } else result += windowsPath && uri[i] == '/' ? '\\' : uri[i];
    }
    return result;
}
std::string plainDocumentation(const Json& value, std::size_t limit) {
    std::string text;
    if (value.is_string()) text = value.get<std::string>();
    else if (value.is_object()) text = value.value("value", "");
    else if (value.is_array()) for (const auto& entry : value) { text += plainDocumentation(entry, limit); text += '\n'; if (text.size() > limit) break; }
    std::string result;
    bool codeFence = false;
    for (std::size_t i = 0; i < text.size() && result.size() < limit; ++i) {
        if (std::string_view(text).substr(i).starts_with("```")) {
            codeFence = !codeFence; i += 2;
            if (codeFence) { while (i + 1 < text.size() && text[i + 1] != '\n') ++i; }
        } else if (static_cast<unsigned char>(text[i]) >= 128) {
            const auto [length, units] = codepoint(text, i);
            (void)units;
            if (result.size() + length > limit) break;
            result.append(text, i, length); i += length - 1;
        } else if (text[i] != '`' && (static_cast<unsigned char>(text[i]) >= 32 || text[i] == '\n' || text[i] == '\t')) result += text[i];
    }
    return result;
}
std::vector<Completion> completions(const Json& result) {
    const auto* items = result.is_array() ? &result : result.is_object() && result.contains("items") ? &result["items"] : nullptr;
    const auto defaults = result.is_object() ? result.value("itemDefaults", Json::object()) : Json::object();
    std::vector<Completion> values;
    if (!items || !items->is_array()) return values;
    for (const auto& input : *items) {
        if (!input.is_object() || !input.contains("label")) continue;
        auto item = input;
        if (!item.contains("insertTextFormat") && defaults.contains("insertTextFormat")) item["insertTextFormat"] = defaults["insertTextFormat"];
        if (!item.contains("data") && defaults.contains("data")) item["data"] = defaults["data"];
        if (!item.contains("textEdit") && defaults.contains("editRange")) {
            item["textEdit"] = defaults["editRange"].contains("start") ? Json{{"range", defaults["editRange"]}} : defaults["editRange"];
            item["textEdit"]["newText"] = item.value("textEditText", item.value("insertText", item.value("label", "")));
        }
        Completion c;
        c.label = item.value("label", ""); c.detail = item.value("detail", "");
        if (item.contains("labelDetails")) c.detail = item["labelDetails"].value("detail", "") + " " + item["labelDetails"].value("description", "") + " " + c.detail;
        c.documentation = plainDocumentation(item.value("documentation", Json{}));
        c.kind = item.value("kind", 1); c.filter = item.value("filterText", c.label); c.sort = item.value("sortText", c.label); c.item = std::move(item);
        values.push_back(std::move(c));
        if (values.size() == 500) break;
    }
    std::stable_sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.sort < b.sort; });
    return values;
}
std::optional<EditPlan> completionEdit(const TextBuffer& buffer, const Completion& completion, std::size_t prefix, std::size_t caret) {
    try {
        const auto& item = completion.item;
        Replacement main{prefix, caret, item.value("insertText", completion.label)};
        if (item.contains("textEdit")) {
            const auto& edit = item["textEdit"];
            const auto r = range(edit.contains("range") ? edit["range"] : edit["replace"]);
            auto a = buffer.offset(r.start), b = buffer.offset(r.end);
            if (!a || !b) return {};
            main = {*a, *b, edit.at("newText").get<std::string>()};
        }
        if (item.value("insertTextFormat", 1) == 2) main.text = snippetText(main.text);
        if (main.start > caret || caret > main.end || main.end > buffer.text().size()) return {};
        EditPlan plan;
        plan.caret = main.start + main.text.size();
        plan.edits.push_back(main);
        bool insertedAtMainStart = false;
        for (const auto& edit : item.value("additionalTextEdits", Json::array())) {
            const auto r = range(edit.at("range"));
            auto a = buffer.offset(r.start), b = buffer.offset(r.end);
            if (!a || !b || *a > *b) return {};
            auto text = edit.at("newText").get<std::string>();
            if (*b <= main.start) plan.caret = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(plan.caret) + text.size() - (*b - *a));
            // Pyrefly may insert an import immediately before the completed name.
            // Merge this shared boundary into the primary replacement so ordering
            // is deterministic and Scintilla never sees overlapping targets.
            if (*a == *b && *a == main.start) {
                if (insertedAtMainStart) return {};
                insertedAtMainStart = true;
                plan.edits.front().text.insert(0, text);
                continue;
            }
            plan.edits.push_back({*a, *b, std::move(text)});
        }
        std::sort(plan.edits.begin(), plan.edits.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
        for (std::size_t i = 0; i < plan.edits.size(); ++i) {
            if (plan.edits[i].start > plan.edits[i].end) return {};
            if (i && (plan.edits[i].end > plan.edits[i - 1].start || plan.edits[i].start == plan.edits[i - 1].start)) return {};
        }
        return plan;
    } catch (...) { return {}; }
}
Signature signature(const Json& result) {
    Signature out;
    if (!result.is_object() || !result.contains("signatures") || result["signatures"].empty()) return out;
    const auto& signatures = result["signatures"];
    const auto selected = std::clamp(result.value("activeSignature", 0), 0, static_cast<int>(signatures.size()) - 1);
    const auto& entry = signatures[selected];
    out.text = entry.value("label", "");
    const int active = entry.value("activeParameter", result.value("activeParameter", 0));
    const auto parameters = entry.value("parameters", Json::array());
    if (active >= 0 && active < static_cast<int>(parameters.size())) {
        const auto& label = parameters[active]["label"];
        if (label.is_array() && label.size() == 2) {
            const auto labelByte = [&](int wanted) {
                int units = 0;
                std::size_t byte = 0;
                while (byte < out.text.size() && units < wanted) { const auto step = codepoint(out.text, byte); byte += step.first; units += step.second; }
                return units == wanted ? static_cast<int>(byte) : 0;
            };
            out.highlightStart = labelByte(label[0].get<int>());
            out.highlightEnd = labelByte(label[1].get<int>());
        } else if (label.is_string()) {
            const auto found = out.text.find(label.get<std::string>());
            if (found != out.text.npos) { out.highlightStart = static_cast<int>(found); out.highlightEnd = out.highlightStart + static_cast<int>(label.get<std::string>().size()); }
        }
    }
    return out;
}
Signature boundedTip(Signature value) {
    Signature out;
    int column = 0, lines = 1;
    for (std::size_t i = 0; i < value.text.size();) {
        const auto [bytes, units] = codepoint(value.text, i);
        (void)units;
        const bool wrap = value.text[i] != '\n' && column == 90;
        if (out.text.size() + bytes + (wrap ? 1 : 0) > 2200 || (wrap || value.text[i] == '\n') && lines == 16) break;
        if (wrap) { out.text += '\n'; ++lines; column = 0; }
        if (i == static_cast<std::size_t>(value.highlightStart)) out.highlightStart = static_cast<int>(out.text.size());
        out.text.append(value.text, i, bytes);
        i += bytes;
        if (i == static_cast<std::size_t>(value.highlightEnd)) out.highlightEnd = static_cast<int>(out.text.size());
        if (value.text[i - bytes] == '\n') { ++lines; column = 0; } else ++column;
    }
    if (out.highlightEnd <= out.highlightStart) out.highlightStart = out.highlightEnd = 0;
    return out;
}
std::vector<Diagnostic> diagnostics(const Json& values) {
    std::vector<Diagnostic> out;
    if (!values.is_array()) return out;
    for (const auto& value : values) {
        try { out.push_back({range(value.at("range")), value.at("message").get<std::string>(), value.value("source", "Python"), value.value("severity", 1)}); }
        catch (...) {}
        if (out.size() == 200) break;
    }
    return out;
}
} // namespace orbit::language
