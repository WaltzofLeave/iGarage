#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

namespace orbit {

// Bounded producer/consumer transport. Backpressure replaces per-chunk window messages.
class RunOutputBuffer {
public:
    explicit RunOutputBuffer(std::size_t capacity = 1024 * 1024);
    bool write(std::string_view text);
    std::string take(std::size_t budget = 256 * 1024);
    void reset();
    void cancel();
    std::size_t peakBytes() const;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<std::string> chunks_;
    std::size_t bytes_ = 0;
    std::size_t offset_ = 0;
    std::size_t peak_ = 0;
    bool canceled_ = false;
};

class OutputLineLimiter {
public:
    explicit OutputLineLimiter(std::size_t maxLineBytes = 16 * 1024);
    std::string consume(std::string_view text);
    void reset();

private:
    const std::size_t maxLineBytes_;
    std::size_t lineBytes_ = 0;
    bool truncated_ = false;
    std::string pendingUtf8_;
};

} // namespace orbit
