#include "animation/SyncTrack.h"
#include "Testing.h"

#include <cmath>
#include <vector>

using namespace ailo;

namespace {

constexpr float kEps = 1e-5f;

constexpr uint32_t kLeft = syncEventId("LeftFoot");
constexpr uint32_t kRight = syncEventId("RightFoot");
constexpr uint32_t kPass = syncEventId("Pass");

// Distance between two clip percents on a loop, where 0 and 1 are the same point.
float loopDistance(float a, float b) {
    const float d = std::fabs(a - b);
    return std::min(d, 1.0f - d);
}

void checkContiguous(const SyncTrack& track) {
    float total = 0.0f;
    for (uint32_t i = 0; i < track.eventCount(); ++i) {
        const SyncEvent& event = track.event(i);
        CHECK_LT(0.0f, event.duration);
        if (i > 0) {
            const SyncEvent& previous = track.event(i - 1);
            CHECK_NEAR(event.start, previous.start + previous.duration, kEps);
        }
        total += event.duration;
    }
    CHECK_NEAR(total, 1.0f, kEps);
}

void testDefaultTrack() {
    const SyncTrack track;
    REQUIRE_EQ(track.eventCount(), 1u);
    CHECK_EQ(track.event(0).id, SyncTrack::kDefaultEventId);
    CHECK_EQ(track.startOffset(), 0.0f);

    CHECK(track.time(0.25f) == (SyncTime { 0, 0.25f }));
    CHECK(track.time(1.0f) == (SyncTime { 0, 1.0f }));
    CHECK(track.time(-1.0f) == (SyncTime { 0, 0.0f }));
    CHECK_NEAR(track.percent({ 0, 0.25f }), 0.25f, kEps);
    CHECK_NEAR(track.percent({ 0, 1.0f }), 1.0f, kEps);

    const SyncTrack empty { std::span<const SyncTrack::Marker> {} };
    CHECK_EQ(empty.eventCount(), 1u);
}

void testMarkers() {
    const std::vector<SyncTrack::Marker> markers = { { kRight, 0.6f }, { kLeft, 0.0f }, { kPass, 0.3f } };
    const SyncTrack track { markers };
    REQUIRE_EQ(track.eventCount(), 3u);
    CHECK_EQ(track.event(0).id, kLeft);
    CHECK_EQ(track.event(1).id, kPass);
    CHECK_EQ(track.event(2).id, kRight);
    CHECK_NEAR(track.event(0).duration, 0.3f, kEps);
    CHECK_NEAR(track.event(2).duration, 0.4f, kEps);
    checkContiguous(track);

    const SyncTime t = track.time(0.45f);
    CHECK_EQ(t.event, 1);
    CHECK_NEAR(t.percent, 0.5f, kEps);
    CHECK_NEAR(track.percent(t), 0.45f, kEps);

    const SyncTime end = track.time(1.0f);
    CHECK_EQ(end.event, 2);
    CHECK_NEAR(end.percent, 1.0f, kEps);
    CHECK_NEAR(track.percent(end), 1.0f, kEps);

    // Event indices past the count wrap, so a track with more events can drive this one.
    CHECK_NEAR(track.percent({ 4, 0.5f }), 0.45f, kEps);

    CHECK_EQ(track.findEvent(kPass).value_or(99), 1);
    CHECK(!track.findEvent(syncEventId("Jump")).has_value());
}

void testStartOffset() {
    const std::vector<SyncTrack::Marker> markers = { { kLeft, 0.25f }, { kRight, 0.75f } };
    const SyncTrack track { markers };
    REQUIRE_EQ(track.eventCount(), 2u);
    CHECK_EQ(track.startOffset(), 0.25f);
    CHECK_NEAR(track.event(1).duration, 0.5f, kEps);
    checkContiguous(track);

    // The clip starts halfway through the right foot event, which wraps around the end.
    const SyncTime start = track.time(0.0f);
    CHECK_EQ(start.event, 1);
    CHECK_NEAR(start.percent, 0.5f, kEps);

    const SyncTime mid = track.time(0.5f);
    CHECK_EQ(mid.event, 0);
    CHECK_NEAR(mid.percent, 0.5f, kEps);

    const SyncTime late = track.time(0.8f);
    CHECK_EQ(late.event, 1);
    CHECK_NEAR(late.percent, 0.1f, kEps);

    CHECK_NEAR(track.percent({ 1, 0.75f }), 0.125f, kEps);
    for (float p : { 0.0f, 0.1f, 0.25f, 0.5f, 0.74f, 0.75f, 0.9f, 1.0f }) {
        CHECK_NEAR(loopDistance(track.percent(track.time(p)), p), 0.0f, kEps);
    }
}

void testMarkerErrors() {
    using Markers = std::vector<SyncTrack::Marker>;
    CHECK_THROWS((SyncTrack { Markers { { kLeft, 1.0f } } }));
    CHECK_THROWS((SyncTrack { Markers { { kLeft, -0.1f } } }));
    CHECK_THROWS((SyncTrack { Markers { { kLeft, std::nanf("") } } }));
    CHECK_THROWS((SyncTrack { Markers { { kLeft, 0.5f }, { kRight, 0.5f } } }));

    Markers tooMany;
    for (uint32_t i = 0; i <= SyncTrack::kMaxEvents; ++i) {
        tooMany.push_back({ i, static_cast<float>(i) / 64.0f });
    }
    CHECK_THROWS(SyncTrack { tooMany });
}

void testBlend() {
    const std::vector<SyncTrack::Marker> walkMarkers = { { kLeft, 0.0f }, { kRight, 0.5f } };
    const std::vector<SyncTrack::Marker> otherMarkers = { { kLeft, 0.1f }, { kPass, 0.3f }, { kRight, 0.6f } };
    const SyncTrack walk { walkMarkers };
    const SyncTrack other { otherMarkers };

    const SyncTrack toWalk { walk, other, 0.0f };
    REQUIRE_EQ(toWalk.eventCount(), 6u);
    CHECK_EQ(toWalk.startOffset(), 0.0f);
    checkContiguous(toWalk);
    for (uint32_t i = 0; i < 6; ++i) {
        CHECK_EQ(toWalk.event(i).id, walk.event(i % 2).id);
        CHECK_NEAR(toWalk.event(i).duration, 1.0f / 6.0f, kEps);
    }

    const SyncTrack toOther { walk, other, 1.0f };
    REQUIRE_EQ(toOther.eventCount(), 6u);
    checkContiguous(toOther);
    for (uint32_t i = 0; i < 6; ++i) {
        CHECK_EQ(toOther.event(i).id, other.event(i % 3).id);
        CHECK_NEAR(toOther.event(i).duration, other.event(i % 3).duration * 0.5f, kEps);
    }

    const SyncTrack half { walk, other, 0.5f };
    checkContiguous(half);
    CHECK_EQ(half.event(1).id, other.event(1).id);
    CHECK_NEAR(half.event(1).duration, 0.5f * (1.0f / 6.0f + 0.15f), kEps);

    // Blending a track with itself keeps its events, though the result starts at 0.
    const std::vector<SyncTrack::Marker> offsetMarkers = { { kLeft, 0.2f }, { kRight, 0.5f } };
    const SyncTrack offset { offsetMarkers };
    const SyncTrack same { offset, offset, 0.3f };
    REQUIRE_EQ(same.eventCount(), 2u);
    CHECK_NEAR(same.event(0).duration, 0.3f, kEps);
    CHECK_NEAR(same.event(1).duration, 0.7f, kEps);
    CHECK_EQ(same.startOffset(), 0.0f);
}

void testBlendFallback() {
    constexpr uint32_t kMax = SyncTrack::kMaxEvents;
    std::vector<SyncTrack::Marker> fewer;
    std::vector<SyncTrack::Marker> most;
    for (uint32_t i = 0; i < kMax; ++i) {
        if (i + 1 < kMax) fewer.push_back({ i, static_cast<float>(i) / static_cast<float>(kMax - 1) });
        most.push_back({ i, static_cast<float>(i) / static_cast<float>(kMax) });
    }
    const SyncTrack blended { SyncTrack { fewer }, SyncTrack { most }, 0.5f };
    CHECK_EQ(blended.eventCount(), kMax);
    checkContiguous(blended);
}

void testBlendedDuration() {
    // A 1s two-step walk and a 1.5s three-event clip both loop 3s over the 6-event blended track.
    CHECK_NEAR(SyncTrack::blendedDuration(1.0f, 2, 1.5f, 3, 6, 0.0f), 3.0f, kEps);
    CHECK_NEAR(SyncTrack::blendedDuration(1.0f, 2, 1.5f, 3, 6, 0.7f), 3.0f, kEps);
    CHECK_NEAR(SyncTrack::blendedDuration(1.0f, 1, 2.0f, 1, 1, 0.5f), 1.5f, kEps);
    CHECK_NEAR(SyncTrack::blendedDuration(1.0f, 1, 2.0f, 1, 1, 2.0f), 2.0f, kEps);
}

void testEventIds() {
    static_assert(syncEventId("LeftFoot") != syncEventId("RightFoot"));
    CHECK_EQ(syncEventId("LeftFoot"), kLeft);
    CHECK_NE(syncEventId(""), SyncTrack::kDefaultEventId);
}

}

void runSyncTrackTests() {
    ailo::test::run("sync_track/testDefaultTrack", testDefaultTrack);
    ailo::test::run("sync_track/testMarkers", testMarkers);
    ailo::test::run("sync_track/testStartOffset", testStartOffset);
    ailo::test::run("sync_track/testMarkerErrors", testMarkerErrors);
    ailo::test::run("sync_track/testBlend", testBlend);
    ailo::test::run("sync_track/testBlendFallback", testBlendFallback);
    ailo::test::run("sync_track/testBlendedDuration", testBlendedDuration);
    ailo::test::run("sync_track/testEventIds", testEventIds);
}
