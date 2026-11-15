#include "FootMarkers.h"
#include "Testing.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

using namespace ailo::model;
using modelc::FootMarkerSettings;
using modelc::findFootMarkers;

namespace {

constexpr float kEps = 1e-4f;

// The pelvis is turned upside down, so the right foot is lowest where its local height is highest.
std::vector<SkeletonNode> makeSkeleton() {
    const glm::mat4 flipped = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0, 5, 0)), glm::pi<float>(),
                                          glm::vec3(0, 0, 1));
    return {
        { "root", -1, glm::mat4(1.0f) },
        { "foot_l", 0, glm::mat4(1.0f) },
        { "pelvis", 0, flipped },
        { "foot_r", 2, glm::mat4(1.0f) },
    };
}

// 1 s, sampled at 10 Hz in the tests. The left foot is up from 0.4 to 0.6 s and down again at 0.7 s; the right
// foot's local height is 10 from 0.6 s on, so upside down it comes down at 0.6 s.
Clip makeClip() {
    Clip clip;
    clip.name = "walk";
    clip.duration = 1.0f;
    Channel& left = clip.channels.emplace_back();
    left.node = "foot_l";
    left.positions = { { 0.0f, { 0, 0, 0 } }, { 0.3f, { 0, 0, 0 } }, { 0.4f, { 0, 10, 0 } },
                       { 0.6f, { 0, 10, 0 } }, { 0.7f, { 0, 0, 0 } }, { 1.0f, { 0, 0, 0 } } };
    Channel& right = clip.channels.emplace_back();
    right.node = "foot_r";
    right.positions = { { 0.0f, { 0, 10, 0 } }, { 0.1f, { 0, 10, 0 } }, { 0.2f, { 0, 0, 0 } },
                        { 0.5f, { 0, 0, 0 } }, { 0.6f, { 0, 10, 0 } }, { 1.0f, { 0, 10, 0 } } };
    return clip;
}

FootMarkerSettings makeSettings() {
    FootMarkerSettings settings;
    settings.feet = { { "LeftFoot", "foot_l" }, { "RightFoot", "foot_r" } };
    settings.groundThreshold = 1.0f;
    settings.sampleRate = 10.0f;
    return settings;
}

void testLandings() {
    const auto markers = findFootMarkers(makeSkeleton(), makeClip(), makeSettings());
    REQUIRE_EQ(markers.size(), 2);
    CHECK_EQ(markers[0].name, "RightFoot");
    CHECK_NEAR(markers[0].time, 0.6f, kEps);
    CHECK_EQ(markers[1].name, "LeftFoot");
    CHECK_NEAR(markers[1].time, 0.7f, kEps);
}

void testSameSample() {
    Clip clip = makeClip();
    clip.channels[1].positions = clip.channels[0].positions;
    for (auto& key : clip.channels[1].positions) key.value.y = 10.0f - key.value.y;

    const auto markers = findFootMarkers(makeSkeleton(), clip, makeSettings());
    REQUIRE_EQ(markers.size(), 1);
    CHECK_EQ(markers[0].name, "LeftFoot");
    CHECK_NEAR(markers[0].time, 0.7f, kEps);
}

void testSpeedThreshold() {
    Clip clip = makeClip();
    clip.channels.pop_back();
    // After coming down at 0.7 s the foot slides until 0.8 s, so it only stops at 0.9 s.
    clip.channels[0].positions = { { 0.0f, { 0, 0, 0 } }, { 0.3f, { 0, 0, 0 } }, { 0.4f, { 0, 10, 0 } },
                                   { 0.6f, { 0, 10, 0 } }, { 0.7f, { 0, 0, 0 } }, { 0.8f, { 100, 0, 0 } },
                                   { 1.0f, { 100, 0, 0 } } };
    FootMarkerSettings settings = makeSettings();
    settings.feet.erase("RightFoot");

    auto markers = findFootMarkers(makeSkeleton(), clip, settings);
    REQUIRE_EQ(markers.size(), 1);
    CHECK_NEAR(markers[0].time, 0.7f, kEps);

    settings.speedThreshold = 50.0f;
    markers = findFootMarkers(makeSkeleton(), clip, settings);
    REQUIRE_EQ(markers.size(), 1);
    CHECK_NEAR(markers[0].time, 0.9f, kEps);
}

void testNoLandings() {
    Clip idle = makeClip();
    idle.channels.clear();
    CHECK(findFootMarkers(makeSkeleton(), idle, makeSettings()).empty());

    Clip empty = makeClip();
    empty.duration = 0.0f;
    CHECK(findFootMarkers(makeSkeleton(), empty, makeSettings()).empty());

    // Everything within the threshold counts as planted the whole time.
    FootMarkerSettings loose = makeSettings();
    loose.groundThreshold = 100.0f;
    CHECK(findFootMarkers(makeSkeleton(), makeClip(), loose).empty());
}

void testErrors() {
    FootMarkerSettings settings = makeSettings();
    settings.feet["Tail"] = "tail";
    CHECK_THROWS(findFootMarkers(makeSkeleton(), makeClip(), settings));

    // A foot bouncing every other sample lands 50 times in 10 s.
    Clip bouncing = makeClip();
    bouncing.duration = 10.0f;
    bouncing.channels.pop_back();
    bouncing.channels[0].positions.clear();
    for (int i = 0; i <= 100; i++) {
        bouncing.channels[0].positions.push_back({ float(i) * 0.1f, { 0, float(i % 2) * 10.0f, 0 } });
    }
    settings = makeSettings();
    settings.feet.erase("RightFoot");
    CHECK_THROWS(findFootMarkers(makeSkeleton(), bouncing, settings));
}

}

void runFootMarkerTests() {
    ailo::test::run("foot_markers/testLandings", testLandings);
    ailo::test::run("foot_markers/testSameSample", testSameSample);
    ailo::test::run("foot_markers/testSpeedThreshold", testSpeedThreshold);
    ailo::test::run("foot_markers/testNoLandings", testNoLandings);
    ailo::test::run("foot_markers/testErrors", testErrors);
}
