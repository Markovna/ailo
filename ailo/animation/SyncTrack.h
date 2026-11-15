#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ailo {

constexpr uint32_t syncEventId(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const char c : name) {
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    return hash;
}

struct SyncEvent {
    uint32_t id = 0;
    float start = 0.0f;
    float duration = 1.0f;
};

struct SyncTime {
    uint16_t event = 0;
    float percent = 0.0f;

    friend constexpr bool operator==(SyncTime, SyncTime) = default;
};

struct SyncTimeRange {
    SyncTime start;
    SyncTime end;
};

class SyncTrack {
public:
    static constexpr uint32_t kMaxEvents = 12;
    static constexpr uint32_t kDefaultEventId = 0;

    struct Marker {
        uint32_t id = 0;
        float start = 0.0f; // fraction of the clip, in [0, 1)
    };

    SyncTrack();

    explicit SyncTrack(std::span<const Marker> markers);

    SyncTrack(const SyncTrack& a, const SyncTrack& b, float weight);

    uint32_t eventCount() const { return m_count; }
    const SyncEvent& event(uint32_t index) const { return m_events[index]; }
    std::span<const SyncEvent> events() const { return std::span(m_events).first(m_count); }
    float startOffset() const { return m_events[0].start; }

    // percent is clamped to [0, 1]. 1 lands at the end of the last event rather than wrapping to the start.
    SyncTime time(float percent) const;

    float percent(SyncTime time) const;

    std::optional<uint16_t> findEvent(uint32_t id) const;

    // How long one loop of the blended track lasts, given the durations of one loop of each source track: a full
    // loop of the blended track plays blendedCount / count loops of each source.
    static float blendedDuration(float durationA, uint32_t countA, float durationB, uint32_t countB,
                                 uint32_t blendedCount, float weight);

private:
    std::array<SyncEvent, kMaxEvents> m_events {};
    uint32_t m_count = 1;
};

} // namespace ailo
