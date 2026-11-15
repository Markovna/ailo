#include "SyncTrack.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <stdexcept>

namespace ailo {

SyncTrack::SyncTrack() {
    m_events[0] = { .id = kDefaultEventId, .start = 0.0f, .duration = 1.0f };
}

SyncTrack::SyncTrack(std::span<const Marker> markers) : SyncTrack() {
    if (markers.empty()) {
        return;
    }
    if (markers.size() > kMaxEvents) {
        throw std::invalid_argument(
            std::format("sync track: {} markers, at most {} are allowed", markers.size(), kMaxEvents));
    }

    std::array<Marker, kMaxEvents> sorted {};
    std::ranges::copy(markers, sorted.begin());
    const auto used = std::span(sorted).first(markers.size());
    std::ranges::sort(used, {}, &Marker::start);

    for (size_t i = 0; i < used.size(); ++i) {
        if (!(used[i].start >= 0.0f && used[i].start < 1.0f)) {
            throw std::invalid_argument(std::format("sync track: marker start {} is outside [0, 1)", used[i].start));
        }
        if (i > 0 && used[i].start == used[i - 1].start) {
            throw std::invalid_argument(std::format("sync track: two markers start at {}", used[i].start));
        }
    }

    m_count = static_cast<uint32_t>(used.size());
    for (uint32_t i = 0; i < m_count; ++i) {
        const float end = i + 1 < m_count ? used[i + 1].start : 1.0f + used[0].start;
        m_events[i] = { .id = used[i].id, .start = used[i].start, .duration = end - used[i].start };
    }
}

SyncTrack::SyncTrack(const SyncTrack& a, const SyncTrack& b, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    const uint32_t countA = a.m_count;
    const uint32_t countB = b.m_count;
    uint32_t count = std::lcm(countA, countB);
    if (count > kMaxEvents) {
        count = std::max(countA, countB);
    }

    const float scaleA = static_cast<float>(countA) / static_cast<float>(count);
    const float scaleB = static_cast<float>(countB) / static_cast<float>(count);
    float total = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        const SyncEvent& eventA = a.m_events[i % countA];
        const SyncEvent& eventB = b.m_events[i % countB];
        const float duration = std::lerp(eventA.duration * scaleA, eventB.duration * scaleB, weight);
        m_events[i] = { .id = weight < 0.5f ? eventA.id : eventB.id, .duration = duration };
        total += duration;
    }

    m_count = count;
    float start = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        SyncEvent& event = m_events[i];
        event.start = start;
        event.duration = total > 0.0f ? event.duration / total : 1.0f / static_cast<float>(count);
        start += event.duration;
    }
    m_events[count - 1].duration = 1.0f - m_events[count - 1].start;
}

SyncTime SyncTrack::time(float percent) const {
    const float offset = startOffset();
    float shifted = std::clamp(percent, 0.0f, 1.0f) - offset;
    if (shifted < 0.0f) {
        shifted += 1.0f;
    }

    uint32_t index = m_count - 1;
    for (uint32_t i = 1; i < m_count; ++i) {
        if (m_events[i].start - offset > shifted) {
            index = i - 1;
            break;
        }
    }

    const SyncEvent& event = m_events[index];
    const float local = shifted - (event.start - offset);
    const float through = event.duration > 0.0f ? std::clamp(local / event.duration, 0.0f, 1.0f) : 0.0f;
    return { .event = static_cast<uint16_t>(index), .percent = through };
}

float SyncTrack::percent(SyncTime time) const {
    const SyncEvent& event = m_events[time.event % m_count];
    float percent = event.start + std::clamp(time.percent, 0.0f, 1.0f) * event.duration;
    // The tolerance keeps the end of a track that starts at 0 from wrapping to 0 over rounding error.
    if (percent > 1.0f + 1e-6f) {
        percent -= 1.0f;
    }
    return std::clamp(percent, 0.0f, 1.0f);
}

std::optional<uint16_t> SyncTrack::findEvent(uint32_t id) const {
    for (uint32_t i = 0; i < m_count; ++i) {
        if (m_events[i].id == id) {
            return static_cast<uint16_t>(i);
        }
    }
    return std::nullopt;
}

float SyncTrack::blendedDuration(float durationA, uint32_t countA, float durationB, uint32_t countB,
                                 uint32_t blendedCount, float weight) {
    const float loopsA = static_cast<float>(blendedCount) / static_cast<float>(countA);
    const float loopsB = static_cast<float>(blendedCount) / static_cast<float>(countB);
    return std::lerp(durationA * loopsA, durationB * loopsB, std::clamp(weight, 0.0f, 1.0f));
}

} // namespace ailo
