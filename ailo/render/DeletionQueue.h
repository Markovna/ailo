#pragma once

#include <cstdint>
#include <deque>
#include <functional>

namespace ailo {

class CommandsPool;

// Delays destruction of GPU objects until no command buffer that could reference them is still executing.
class DeletionQueue {
public:
    explicit DeletionQueue(CommandsPool& commands) : m_commands(commands) {}

    DeletionQueue(const DeletionQueue&) = delete;
    DeletionQueue& operator=(const DeletionQueue&) = delete;

    // Runs `destroy` once every command buffer recorded so far (including the one being recorded) has completed.
    void defer(std::move_only_function<void()> destroy);

    // Runs the pending destructions whose command buffers have completed. Call once per frame; no-op after shutdown().
    void collect();

    // Runs everything still pending and makes later defer() calls run immediately.
    // Only valid once the device is idle and no more frames will be recorded.
    void shutdown();

private:
    struct Entry {
        uint64_t serial;
        std::move_only_function<void()> destroy;
    };

    CommandsPool& m_commands;
    std::deque<Entry> m_entries;
    bool m_immediate = false;
};

}
