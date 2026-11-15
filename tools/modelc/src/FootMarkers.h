#pragma once

#include <map>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/model/ModelPackage.h"

namespace modelc {

struct FootMarkerSettings {
    std::map<std::string, std::string> feet;    // marker name -> foot bone
    std::vector<std::string> clips;             // the clips to mark; empty marks every clip
    float groundThreshold = 0.0f;               // source units above the foot's lowest point that count as planted
    float speedThreshold = 0.0f;                // source units per second a planted foot moves under; 0 skips the test
    float sampleRate = 60.0f;                   // samples per second
    glm::vec3 up { 0.0f, 1.0f, 0.0f };          // unit length, in the model's space
};

// Samples each foot bone in model space and puts a marker, named after the foot, at every sample where the foot comes
// down: it is planted (within groundThreshold of its lowest point in the clip, and slower than speedThreshold) and was
// not at the sample before. The first sample follows the last, as the clip loops. A foot that is always
// or never planted gets no markers. When two feet come down at the same sample, the marker named first is kept. Throws
// std::runtime_error if a foot bone isn't in the skeleton or the clip gets more than SyncTrack::kMaxEvents markers.
std::vector<ailo::model::SyncMarker> findFootMarkers(std::span<const ailo::model::SkeletonNode> skeleton,
                                                     const ailo::model::Clip& clip,
                                                     const FootMarkerSettings& settings);

}
