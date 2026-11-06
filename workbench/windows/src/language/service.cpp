#include "service.h"
#include <stdexcept>
#include <utility>

namespace orbit::language {
Service::~Service() { stop(); }
void Service::start(std::unique_ptr<Transport> transport, SessionOptions options, Transport::Wake wake) {
    stop();
    started_ = std::chrono::steady_clock::now();
    initializationMs_ = 0; staleResponses_ = 0;
    status_ = "正在启动代码分析…";
    try {
        if (!transport) throw std::invalid_argument("Language session requires a transport");
        uris_ = WorkspaceUris(options.rootUri);
        options_ = std::move(options);
        transport_ = std::move(transport);
        transport_->start(std::move(wake)); initializing_ = true;
    } catch (const std::exception& error) {
        if (transport_) { transport_->stop(); transport_.reset(); }
        status_ = error.what();
    }
}
void Service::stop() {
    if (transport_) { transport_->stop(); transport_.reset(); }
    documents_.clear(); pending_.clear(); ready_ = initializing_ = canResolve_ = false;
    diagnosticsChanged_ = true;
}
bool Service::send(Json value) {
    value["jsonrpc"] = "2.0";
    if (transport_ && transport_->send(std::move(value))) return true;
    ready_ = false;
    status_ = "代码分析连接不可用，可从工具菜单重新启动";
    return false;
}
std::string Service::uri(const std::string& path) const { return uris_.document(path); }
Json Service::configuration(std::string_view section) const {
    const auto* value = &options_.settings;
    while (!section.empty()) {
        const auto dot = section.find('.');
        const auto component = std::string(section.substr(0, dot));
        if (!value->is_object() || !value->contains(component)) return nullptr;
        value = &(*value)[component];
        if (dot == section.npos) break;
        section.remove_prefix(dot + 1);
    }
    return *value;
}
void Service::sendOpen(const std::string& path, OpenDocument& document) {
    if (!ready_ || document.sent) return;
    document.sent = send({{"method", "textDocument/didOpen"}, {"params", {{"textDocument", {
        {"uri", uri(path)}, {"languageId", options_.languageId}, {"version", document.revision}, {"text", document.buffer.text()}}}}}});
}
void Service::open(std::string path, std::string text, std::uint64_t revision) {
    if (!WorkspaceUris::validRelative(path) || text.size() > fileLimit || documents_.size() >= 64 && !documents_.contains(path)) return;
    const auto found = documents_.find(path);
    if (found != documents_.end() && found->second.revision == revision && found->second.buffer.text() == text) return;
    close(path);
    auto& document = documents_[path];
    document.buffer = TextBuffer(std::move(text)); document.revision = revision;
    sendOpen(path, document);
}
void Service::close(const std::string& path) {
    cancelRequests(path);
    if (const auto found = documents_.find(path); found != documents_.end()) {
        if (ready_ && found->second.sent) send({{"method", "textDocument/didClose"}, {"params", {{"textDocument", {{"uri", uri(path)}}}}}});
        documents_.erase(found); diagnosticsChanged_ = true;
    }
}
const OpenDocument* Service::document(const std::string& path) const {
    const auto it = documents_.find(path);
    return it == documents_.end() ? nullptr : &it->second;
}
bool Service::change(const std::string& path, std::size_t byte, std::size_t removed, std::string_view inserted, std::uint64_t revision) {
    auto found = documents_.find(path);
    if (found == documents_.end()) return false;
    cancelRequests(path);
    auto& document = found->second;
    if (document.buffer.text().size() - std::min(removed, document.buffer.text().size()) + inserted.size() > fileLimit) { close(path); return false; }
    try {
        auto edit = document.buffer.change(byte, removed, inserted);
        document.revision = revision;
        document.diagnostics.clear(); diagnosticsChanged_ = true;
        if (ready_ && document.sent) {
            if (!incremental_) edit = {{"text", document.buffer.text()}};
            return send({{"method", "textDocument/didChange"}, {"params", {
                {"textDocument", {{"uri", uri(path)}, {"version", revision}}}, {"contentChanges", Json::array({edit})}}}});
        }
        return true;
    } catch (...) { close(path); return false; }
}
void Service::cancelRequests(const std::string& path) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.path != path) { ++it; continue; }
        if (ready_) send({{"method", "$/cancelRequest"}, {"params", {{"id", it->first}}}});
        it = pending_.erase(it);
    }
}
bool Service::request(RequestKind kind, const std::string& path, std::size_t caret, Json item) {
    auto found = documents_.find(path);
    if (!ready_ || found == documents_.end()) return false;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second.kind == kind) { send({{"method", "$/cancelRequest"}, {"params", {{"id", it->first}}}}); it = pending_.erase(it); }
        else ++it;
    }
    try {
        Json parameters{{"textDocument", {{"uri", uri(path)}}}, {"position", encode(found->second.buffer.at(caret))}};
        std::string method;
        switch (kind) {
        case RequestKind::Completion: method = "textDocument/completion"; parameters["context"] = {{"triggerKind", 1}}; break;
        case RequestKind::Resolve: method = "completionItem/resolve"; parameters = std::move(item); break;
        case RequestKind::Signature: method = "textDocument/signatureHelp"; break;
        case RequestKind::Hover: method = "textDocument/hover"; break;
        }
        const int id = nextId_++;
        pending_.emplace(id, Pending{kind, path, found->second.revision, caret, std::chrono::steady_clock::now()});
        if (!send({{"id", id}, {"method", method}, {"params", std::move(parameters)}})) { pending_.erase(id); return false; }
        return true;
    } catch (...) { return false; }
}
void Service::serverRequest(const Json& message) {
    const auto method = message.value("method", "");
    Json result = nullptr;
    if (method == "workspace/configuration") {
        result = Json::array();
        for (const auto& item : message["params"]["items"]) {
            const auto section = item.value("section", "");
            result.push_back(configuration(section));
        }
    } else if (method == "workspace/workspaceFolders") result = Json::array({{{"uri", uris_.root()}, {"name", options_.workspaceName}}});
    else if (method == "workspace/applyEdit") result = {{"applied", false}, {"failureReason", "Use an explicit editor action to apply changes"}};
    else if (method != "client/registerCapability" && method != "client/unregisterCapability" && method != "window/workDoneProgress/create" &&
             method != "workspace/diagnostic/refresh" && method != "workspace/semanticTokens/refresh" && method != "workspace/inlayHint/refresh") {
        send({{"id", message["id"]}, {"error", {{"code", -32601}, {"message", "Unsupported client method"}}}}); return;
    }
    send({{"id", message["id"]}, {"result", result}});
}
std::vector<Result> Service::poll() {
    diagnosticsChanged_ = false;
    std::vector<Result> results;
    if (!transport_) return results;
    for (auto& event : transport_->drain()) {
        try {
            if (event.kind == TransportEvent::Kind::Connected) {
                Json capabilities{
                    {"general", {{"positionEncodings", {"utf-16"}}}},
                    {"workspace", {{"configuration", true}, {"workspaceFolders", true}}},
                    {"textDocument", {
                        {"synchronization", {{"dynamicRegistration", false}, {"didSave", false}}},
                        {"publishDiagnostics", {{"versionSupport", true}}},
                        {"completion", {{"completionItem", {{"snippetSupport", false}, {"insertReplaceSupport", true},
                            {"labelDetailsSupport", true}, {"documentationFormat", {"plaintext", "markdown"}},
                            {"resolveSupport", {{"properties", {"documentation", "detail", "additionalTextEdits"}}}}}},
                            {"completionList", {{"itemDefaults", {"editRange", "insertTextFormat", "data"}}}}}},
                        {"signatureHelp", {{"signatureInformation", {{"parameterInformation", {{"labelOffsetSupport", true}}}, {"activeParameterSupport", true}}}}},
                        {"hover", {{"contentFormat", {"plaintext", "markdown"}}}}
                    }}};
                send({{"id", 1}, {"method", "initialize"}, {"params", {{"processId", options_.parentProcessId ? Json(*options_.parentProcessId) : Json(nullptr)},
                    {"clientInfo", {{"name", "Orbit Native"}, {"version", "1.1"}}}, {"rootUri", uris_.root()},
                    {"workspaceFolders", Json::array({{{"uri", uris_.root()}, {"name", options_.workspaceName}}})},
                    {"capabilities", capabilities}, {"initializationOptions", options_.initializationOptions}}}});
                continue;
            }
            if (event.kind == TransportEvent::Kind::Disconnected) {
                ready_ = initializing_ = false; status_ = event.error; pending_.clear();
                for (auto& [path, document] : documents_) document.diagnostics.clear();
                diagnosticsChanged_ = true; continue;
            }
            const auto& message = event.message;
            if (message.contains("method")) {
                if (message.contains("id")) { serverRequest(message); continue; }
                if (message["method"] == "textDocument/publishDiagnostics" && ready_) {
                    const auto& parameters = message["params"];
                    const auto path = uris_.relative(parameters.value("uri", ""));
                    const auto found = path ? documents_.find(*path) : documents_.end();
                    if (found == documents_.end()) continue;
                    auto& document = found->second;
                    if (parameters.contains("version") && !parameters["version"].is_null() && parameters["version"].get<std::uint64_t>() != document.revision) { ++staleResponses_; continue; }
                    document.diagnostics = language::diagnostics(parameters.value("diagnostics", Json::array()));
                    diagnosticsChanged_ = true;
                }
                continue;
            }
            if (!message.contains("id") || !message["id"].is_number_integer()) continue;
            const int id = message["id"];
            if (id == 1) {
                initializing_ = false;
                if (message.contains("error")) { status_ = "代码分析初始化失败"; continue; }
                const auto capabilities = message["result"].value("capabilities", Json::object());
                if (capabilities.value("positionEncoding", "utf-16") != "utf-16") { status_ = "代码分析返回了不支持的位置编码"; continue; }
                const auto sync = capabilities.value("textDocumentSync", Json(2));
                incremental_ = sync.is_number_integer() ? sync.get<int>() == 2 : sync.value("change", 2) == 2;
                canResolve_ = capabilities.contains("completionProvider") && capabilities["completionProvider"].is_object() &&
                    capabilities["completionProvider"].value("resolveProvider", false);
                ready_ = true;
                status_ = "代码分析就绪 · " + options_.serverName;
                initializationMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();
                send({{"method", "initialized"}, {"params", Json::object()}});
                send({{"method", "workspace/didChangeConfiguration"}, {"params", {{"settings", options_.settings}}}});
                for (auto& [path, document] : documents_) sendOpen(path, document);
                continue;
            }
            const auto pending = pending_.find(id);
            if (pending == pending_.end()) { ++staleResponses_; continue; }
            const auto request = pending->second;
            pending_.erase(pending);
            const auto doc = documents_.find(request.path);
            if (doc == documents_.end() || doc->second.revision != request.revision) { ++staleResponses_; continue; }
            if (message.contains("result")) results.push_back({request.kind, request.path, request.revision, request.caret, message["result"],
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - request.started).count()});
            else if (message.contains("error")) results.push_back({request.kind, request.path, request.revision, request.caret, nullptr, 0,
                message["error"].value("message", "Code insight request failed")});
        } catch (const std::exception&) { status_ = "代码分析响应无效"; }
    }
    // A stalled provider cannot retain requests indefinitely.
    if (initializing_ && std::chrono::steady_clock::now() - started_ > std::chrono::seconds(15)) {
        transport_->stop(); initializing_ = false; status_ = "代码分析启动超时，可从工具菜单重新启动";
    }
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (std::chrono::steady_clock::now() - it->second.started > std::chrono::seconds(10)) {
            const auto& request = it->second;
            results.push_back({request.kind, request.path, request.revision, request.caret, nullptr, 10000, "Code insight request timed out"});
            send({{"method", "$/cancelRequest"}, {"params", {{"id", it->first}}}}); it = pending_.erase(it);
        } else ++it;
    }
    return results;
}
} // namespace orbit::language
