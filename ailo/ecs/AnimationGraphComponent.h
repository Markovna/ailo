#pragma once

#include <optional>
#include <vector>

#include "AnimGraph.h"
#include "assets/AssetServer.h"
#include "render/Animation.h"

namespace ailo {

struct AnimationGraphComponent {
    std::vector<AssetPtr<AnimationClip>> clips;
    bool playing = true;
    std::optional<AnimGraphRunner> graph;

    void setGraph(AssetPtr<AnimGraph> definition) {
        graph.emplace(std::move(definition), clips);
    }
};

} // namespace ailo
