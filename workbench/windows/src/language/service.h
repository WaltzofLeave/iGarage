#pragma once
#include "client.h"
#include <chrono>
#include <map>

namespace orbit::language {
enum class RequestKind { Completion, Resolve, Signature, Hover };
struct Result {
    RequestKind kind;
    std::string path;
    std::uint64_t revision = 0;
    std::size_t caret = 0;
    Json value;
    double milliseconds = 0;
    std::string error;
};
struct OpenDocument {
    TextBuffer buffer;
    std::uint64_t revision = 0;
    bool sent = false;
    std::vector<Diagnostic> diagnostics;
};

class Service {
public:
    static constexpr std::size_t fileLimit = 2 * 1024 * 1024;
    void start(ProcessOptions options, std::string interpreter, std::function<void()> wake);
    void stop();
    void open(std::string path, std::string text, std::uint64_t revision);
    void close(const std::string& path);
    bool change(const std::string& path, std::size_t byte, std::size_t removed,
                std::string_view inserted, std::uint64_t revision);
    bool request(RequestKind kind, const std::string& path, std::size_t caret, Json item = {});
    void cancelRequests(const std::string& path);
    std::vector<Result> poll();
    const OpenDocument* document(const std::string& path) const;
    const std::map<std::string, OpenDocument>& documents() const { return documents_; }
    bool ready() const { return ready_; }
    bool canResolve() const { return canResolve_; }
    const std::string& status() const { return status_; }
    bool diagnosticsChanged() const { return diagnosticsChanged_; }
    double initializationMs() const { return initializationMs_; }
    DWORD processId() const { return client_.processId(); }
    std::size_t peakQueuedBytes() const { return client_.peakQueuedBytes(); }
    std::size_t staleResponses() const { return staleResponses_; }
private:
    struct Pending {
        RequestKind kind;
        std::string path;
        std::uint64_t revision;
        std::size_t caret;
        std::chrono::steady_clock::time_point started;
    };
    bool send(Json value);
    void sendOpen(const std::string& path, OpenDocument& document);
    void serverRequest(const Json& message);
    Json configuration() const;
    std::string uri(const std::string& path) const;
    Client client_;
    std::filesystem::path root_;
    std::string interpreter_;
    std::map<std::string, OpenDocument> documents_;
    std::map<int, Pending> pending_;
    int nextId_ = 2;
    bool ready_ = false, initializing_ = false, incremental_ = true, diagnosticsChanged_ = false, canResolve_ = false;
    std::string status_ = "代码分析未启动";
    std::chrono::steady_clock::time_point started_;
    double initializationMs_ = 0;
    std::size_t staleResponses_ = 0;
};
} // namespace orbit::language
