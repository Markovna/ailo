#include "DeletionQueue.h"

#include "CommandBuffer.h"

namespace ailo {

void DeletionQueue::defer(std::move_only_function<void()> destroy) {
    if (m_immediate) {
        destroy();
        return;
    }

    m_entries.push_back({ m_commands.currentSerial(), std::move(destroy) });
}

void DeletionQueue::collect() {
    if (m_entries.empty()) {
        return;
    }

    const uint64_t completed = m_commands.completedSerial();

    while (!m_entries.empty() && m_entries.front().serial <= completed) {
        auto destroy = std::move(m_entries.front().destroy);
        m_entries.pop_front();
        destroy();
    }
}

void DeletionQueue::shutdown() {
    m_immediate = true;

    while (!m_entries.empty()) {
        auto destroy = std::move(m_entries.front().destroy);
        m_entries.pop_front();
        destroy();
    }
}

}
