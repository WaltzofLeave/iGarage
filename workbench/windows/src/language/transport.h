#pragma once
#include "protocol.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace orbit::language {
struct TransportEvent {
    enum class Kind { Connected, Message, Disconnected } kind;
    Json message;
    std::string error;
};

// Service owns the transport and calls this interface on one host thread.
// Implementations perform connection/I/O on workers and deliver parsed JSON-RPC
// through drain(). send() queues without waiting for the peer. Wake only signals
// the host; it must not call Service/editor methods. stop() joins/cleans workers
// and guarantees no later callbacks. Reconnect by starting a new transport.
class Transport {
public:
    using Wake = std::function<void()>;
    virtual ~Transport() = default;
    virtual void start(Wake wake) = 0;
    virtual void stop() noexcept = 0;
    virtual bool send(Json message) = 0;
    virtual std::vector<TransportEvent> drain() = 0;
    virtual std::size_t peakQueuedBytes() const = 0;
    // Optional local-process telemetry. Remote transports do not expose a PID.
    virtual std::uint32_t processId() const { return 0; }
};
} // namespace orbit::language
