#include "output_buffer.h"

#include <algorithm>

namespace orbit {

RunOutputBuffer::RunOutputBuffer(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

bool RunOutputBuffer::write(std::string_view text) {
    while (!text.empty()) {
        const auto length = std::min(text.size(), capacity_);
        std::unique_lock lock(mutex_);
        available_.wait(lock, [&] { return canceled_ || bytes_ + length <= capacity_; });
        if (canceled_) return false;
        chunks_.emplace_back(text.substr(0, length));
        bytes_ += length;
        peak_ = std::max(peak_, bytes_);
        text.remove_prefix(length);
    }
    return true;
}

std::string RunOutputBuffer::take(std::size_t budget) {
    std::unique_lock lock(mutex_);
    std::string result;
    result.reserve(std::min(budget, bytes_));
    while (!chunks_.empty() && result.size() < budget) {
        const auto length = std::min(budget - result.size(), chunks_.front().size() - offset_);
        result.append(chunks_.front(), offset_, length);
        offset_ += length;
        bytes_ -= length;
        if (offset_ == chunks_.front().size()) {
            chunks_.pop_front();
            offset_ = 0;
        }
    }
    lock.unlock();
    available_.notify_all();
    return result;
}

void RunOutputBuffer::reset() {
    std::lock_guard lock(mutex_);
    chunks_.clear();
    bytes_ = offset_ = peak_ = 0;
    canceled_ = false;
}

void RunOutputBuffer::cancel() {
    { std::lock_guard lock(mutex_); canceled_ = true; }
    available_.notify_all();
}

std::size_t RunOutputBuffer::peakBytes() const {
    std::lock_guard lock(mutex_);
    return peak_;
}

OutputLineLimiter::OutputLineLimiter(std::size_t maxLineBytes)
    : maxLineBytes_(std::max<std::size_t>(4, maxLineBytes)) {}

void OutputLineLimiter::reset() {
    lineBytes_ = 0;
    truncated_ = false;
    pendingUtf8_.clear();
}

std::string OutputLineLimiter::consume(std::string_view text) {
    std::string joined;
    if (!pendingUtf8_.empty()) {
        joined = std::move(pendingUtf8_);
        joined.append(text);
        text = joined;
        pendingUtf8_.clear();
    }
    std::string result;
    result.reserve(std::min(text.size(), maxLineBytes_ + 80));
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\n') {
            if (!truncated_) result += '\n';
            lineBytes_ = 0;
            truncated_ = false;
            ++i;
            continue;
        }
        std::size_t length = byte >= 0xc2 && byte <= 0xdf ? 2 :
            byte >= 0xe0 && byte <= 0xef ? 3 : byte >= 0xf0 && byte <= 0xf4 ? 4 : 1;
        if (i + length > text.size()) {
            pendingUtf8_.assign(text.substr(i));
            break;
        }
        for (std::size_t j = 1; j < length; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) { length = 1; break; }
        }
        if (!truncated_ && lineBytes_ + length <= maxLineBytes_) {
            result.append(text.substr(i, length));
            lineBytes_ = byte == '\r' ? 0 : lineBytes_ + length;
        } else if (!truncated_) {
            result += " … [Orbit: long output line truncated]\n";
            truncated_ = true;
        }
        i += length;
    }
    return result;
}

} // namespace orbit
