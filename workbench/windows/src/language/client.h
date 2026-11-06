#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "protocol.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace orbit::language {
struct ProcessOptions {
    std::filesystem::path executable, root;
    std::vector<std::wstring> arguments{L"lsp"};
};
struct ProcessEvent {
    enum class Kind { Started, Message, Stopped } kind;
    Json message;
    std::string error;
};

// No UI or editor calls occur on these threads. The wake callback only posts a message.
class Client {
public:
    ~Client();
    void start(ProcessOptions options, std::function<void()> wake);
    void stop();
    bool send(Json message);
    std::vector<ProcessEvent> drain();
    DWORD processId() const { return processId_; }
    std::size_t peakQueuedBytes() const { return peakQueued_; }
private:
    void run(ProcessOptions options);
    void emit(ProcessEvent event);
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Json> outgoing_;
    std::deque<ProcessEvent> incoming_;
    std::size_t queuedBytes_ = 0, incomingBytes_ = 0;
    std::atomic<std::size_t> peakQueued_{0};
    std::atomic<bool> stopping_{true};
    std::atomic<DWORD> processId_{0};
    std::thread worker_;
    HANDLE job_ = nullptr;
    std::function<void()> wake_;
};
} // namespace orbit::language
