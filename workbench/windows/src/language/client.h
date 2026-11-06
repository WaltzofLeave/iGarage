#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "transport.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace orbit::language {
struct ProcessOptions {
    std::filesystem::path executable, root;
    std::vector<std::wstring> arguments{L"lsp"};
};
// No UI or editor calls occur on these threads. The wake callback only posts a message.
class ProcessTransport final : public Transport {
public:
    explicit ProcessTransport(ProcessOptions options) : options_(std::move(options)) {}
    ~ProcessTransport() override;
    void start(Wake wake) override;
    void stop() noexcept override;
    bool send(Json message) override;
    std::vector<TransportEvent> drain() override;
    std::uint32_t processId() const override { return processId_; }
    std::size_t peakQueuedBytes() const override { return peakQueued_; }
private:
    void run(ProcessOptions options);
    void emit(TransportEvent event);
    ProcessOptions options_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Json> outgoing_;
    std::deque<TransportEvent> incoming_;
    std::size_t queuedBytes_ = 0, incomingBytes_ = 0;
    std::atomic<std::size_t> peakQueued_{0};
    std::atomic<bool> stopping_{true};
    std::atomic<DWORD> processId_{0};
    std::thread worker_;
    HANDLE job_ = nullptr;
    std::function<void()> wake_;
};
} // namespace orbit::language
