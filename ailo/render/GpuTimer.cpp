#include "GpuTimer.h"

#include <cassert>

#include "CommandBuffer.h"

namespace ailo {

namespace {

constexpr uint32_t kNoScope = UINT32_MAX;
constexpr float kSmoothing = 0.1f;

}

GpuTimer::GpuTimer(vk::Device device, vk::PhysicalDevice physicalDevice, uint32_t queueFamilyIndex, CommandsPool& commands)
    : m_device(device), m_commands(commands) {
    const uint32_t validBits = physicalDevice.getQueueFamilyProperties()[queueFamilyIndex].timestampValidBits;
    if (validBits == 0) {
        return;
    }
    m_validMask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1;
    m_nsPerTick = physicalDevice.getProperties().limits.timestampPeriod;

    vk::QueryPoolCreateInfo poolInfo{};
    poolInfo.queryType = vk::QueryType::eTimestamp;
    poolInfo.queryCount = kFrames * kMaxScopes * 2;
    m_pool = device.createQueryPool(poolInfo);
}

GpuTimer::~GpuTimer() {
    assert(!m_pool && "call destroy() once the device is idle");
}

void GpuTimer::destroy() {
    if (m_pool) {
        m_device.destroyQueryPool(m_pool);
        m_pool = nullptr;
    }
}

void GpuTimer::beginFrame(vk::CommandBuffer commandBuffer) {
    if (!m_pool) {
        return;
    }
    collect();

    Frame& frame = m_frames[m_current];
    // Only when more frames are in flight than there are slots: this frame goes untimed.
    if (frame.pending) {
        return;
    }

    commandBuffer.resetQueryPool(m_pool, firstQuery(m_current), kMaxScopes * 2);
    frame.scopes.clear();
    m_open.clear();
    m_recording = true;

    begin(commandBuffer, "Frame");
}

void GpuTimer::endFrame(vk::CommandBuffer commandBuffer) {
    if (!m_recording) {
        return;
    }
    while (!m_open.empty()) {
        end(commandBuffer);
    }

    Frame& frame = m_frames[m_current];
    frame.serial = m_commands.currentSerial();
    frame.pending = true;
    m_current = (m_current + 1) % kFrames;
    m_recording = false;
}

void GpuTimer::begin(vk::CommandBuffer commandBuffer, std::string_view name) {
    Frame& frame = m_frames[m_current];
    if (!m_recording || frame.scopes.size() == kMaxScopes) {
        m_open.push_back(kNoScope);
        return;
    }

    const auto index = static_cast<uint32_t>(frame.scopes.size());
    frame.scopes.push_back({ std::string(name), static_cast<uint32_t>(m_open.size()) });
    m_open.push_back(index);
    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, m_pool, firstQuery(m_current) + index * 2);
}

void GpuTimer::end(vk::CommandBuffer commandBuffer) {
    assert(!m_open.empty() && "GpuTimer::end without begin");
    if (m_open.empty()) {
        return;
    }
    const uint32_t index = m_open.back();
    m_open.pop_back();
    if (index == kNoScope) {
        return;
    }
    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_pool, firstQuery(m_current) + index * 2 + 1);
}

void GpuTimer::collect() {
    const uint64_t completed = m_commands.completedSerial();
    // Oldest slot first, so the newest completed frame is resolved last and ends up in m_timings.
    for (uint32_t i = 0; i < kFrames; i++) {
        const uint32_t frameIndex = (m_current + i) % kFrames;
        Frame& frame = m_frames[frameIndex];
        if (frame.pending && frame.serial <= completed) {
            resolve(frameIndex);
            frame.pending = false;
        }
    }
}

void GpuTimer::resolve(uint32_t frameIndex) {
    const Frame& frame = m_frames[frameIndex];
    if (frame.scopes.empty()) {
        return;
    }

    std::array<uint64_t, kMaxScopes * 2> ticks{};
    const auto queryCount = static_cast<uint32_t>(frame.scopes.size() * 2);
    // The pointer overload returns eNotReady instead of throwing.
    const vk::Result result = m_device.getQueryPoolResults(m_pool, firstQuery(frameIndex), queryCount,
        queryCount * sizeof(uint64_t), ticks.data(), sizeof(uint64_t), vk::QueryResultFlagBits::e64);
    if (result != vk::Result::eSuccess) {
        return;
    }

    std::vector<GpuTiming> timings;
    timings.reserve(frame.scopes.size());
    for (size_t i = 0; i < frame.scopes.size(); i++) {
        const uint64_t elapsed = (ticks[i * 2 + 1] - ticks[i * 2]) & m_validMask;
        float ms = static_cast<float>(static_cast<double>(elapsed) * m_nsPerTick * 1e-6);

        // Smooth only while the frame keeps the same shape, so scopes don't blend into unrelated ones.
        if (i < m_timings.size() && m_timings[i].name == frame.scopes[i].name) {
            ms = m_timings[i].ms + (ms - m_timings[i].ms) * kSmoothing;
        }
        timings.push_back({ frame.scopes[i].name, frame.scopes[i].depth, ms });
    }
    m_timings = std::move(timings);
}

}
