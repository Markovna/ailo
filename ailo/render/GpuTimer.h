#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace ailo {

class CommandsPool;

struct GpuTiming {
    std::string name;
    uint32_t depth = 0; // nesting level, 0 for the whole frame
    float ms = 0.0f;    // smoothed over recent frames
};

// Per-frame GPU timestamp queries. Results are read back without waiting, once the frame's command buffer completes,
// so timings() lags the frame being recorded by the frames in flight.
class GpuTimer {
public:
    GpuTimer(vk::Device device, vk::PhysicalDevice physicalDevice, uint32_t queueFamilyIndex, CommandsPool& commands);
    ~GpuTimer();

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // Must be recorded outside a render pass; it resets the frame's queries.
    void beginFrame(vk::CommandBuffer commandBuffer);
    void endFrame(vk::CommandBuffer commandBuffer);

    // Scopes nest. Ignored outside a frame, or when the frame ran out of queries.
    void begin(vk::CommandBuffer commandBuffer, std::string_view name);
    void end(vk::CommandBuffer commandBuffer);

    // The latest completed frame; the first entry is the whole frame.
    std::span<const GpuTiming> timings() const { return m_timings; }

    // Only valid once the device is idle.
    void destroy();

private:
    struct Scope {
        std::string name;
        uint32_t depth = 0;
    };

    struct Frame {
        std::vector<Scope> scopes; // scope i uses queries 2i and 2i + 1
        uint64_t serial = 0;
        bool pending = false;      // submitted, results not read yet
    };

    static constexpr uint32_t kFrames = 16;
    static constexpr uint32_t kMaxScopes = 32;

    void collect();
    void resolve(uint32_t frameIndex);
    uint32_t firstQuery(uint32_t frameIndex) const { return frameIndex * kMaxScopes * 2; }

    vk::Device m_device;
    CommandsPool& m_commands;
    vk::QueryPool m_pool;
    double m_nsPerTick = 0.0;
    uint64_t m_validMask = 0;

    std::array<Frame, kFrames> m_frames;
    uint32_t m_current = 0;
    bool m_recording = false;
    std::vector<uint32_t> m_open; // indices of the scopes begun and not yet ended

    std::vector<GpuTiming> m_timings;
};

}
