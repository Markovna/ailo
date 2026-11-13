#pragma once

#include <optional>
#include <vector>

#include "AnimGraph.h"
#include "assets/AssetServer.h"
#include "render/Animation.h"

namespace ailo {

struct AnimatorComponent {
    std::vector<AssetPtr<AnimationClip>> clips;
    bool playing = true;
    std::optional<AnimGraphRunner> graph;

    // Starts in the root machine's entry state, without a fade. Throws std::invalid_argument if a clip node's clip
    // isn't in clips.
    void setGraph(AssetPtr<AnimGraph> definition) {
        graph.emplace(std::move(definition), clips);
    }
};

} // namespace ailo
