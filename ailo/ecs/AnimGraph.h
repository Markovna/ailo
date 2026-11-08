#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "AnimationBlend.h"
#include "assets/AssetServer.h"
#include "render/Animation.h"
#include "render/Skeleton.h"

namespace ailo {

struct ClipNode {
    std::string clip;        // AnimationClip::name
    float       rate    = 1.0f;
    bool        looping = true;
};

inline constexpr uint32_t kAnimNone = std::numeric_limits<uint32_t>::max();

struct AnimState {
    uint32_t node            = kAnimNone; // index into the graph's nodes
    uint32_t firstTransition = 0;         // into the machine's transitions
    uint32_t transitionCount = 0;
};

struct AnimTransition {
    uint32_t to           = 0;            // state of the same machine
    uint32_t trigger      = kAnimNone;
    // Seconds the from-state must have been active before the transition can fire.
    float    exitTime     = 0.0f;
    float    fadeDuration = 0.0f;
};

// State and transition indices are local to their machine.
struct StateMachineNode {
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions; // grouped by from-state
    uint32_t entryState = 0;
};

using AnimNode = std::variant<ClipNode, StateMachineNode>;

// Immutable once shared: a tree of nodes in a flat array that refer to each other by index. Node 0 is the root
// machine.
class AnimGraph {
public:
    static constexpr uint32_t kNone = kAnimNone;
    using State = AnimState;
    using Transition = AnimTransition;

    AnimGraph();

    // Adds a clip state to the root machine and returns its state index.
    uint32_t addState(std::string clip, bool looping = true);
    uint32_t addTrigger(const std::string& name);
    void addTransition(uint32_t from, uint32_t to, const std::string& trigger = {}, float fadeDuration = 0.0f,
                       float exitTime = 0.0f);

    const AnimNode& node(uint32_t index) const { return m_nodes[index]; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
    const StateMachineNode& root() const { return std::get<StateMachineNode>(m_nodes[0]); }

    const StateMachineNode& machine(uint32_t node) const { return std::get<StateMachineNode>(m_nodes[node]); }

    // The root machine's states.
    uint32_t stateCount() const { return static_cast<uint32_t>(root().states.size()); }
    const State& state(uint32_t index) const { return root().states[index]; }
    std::span<const Transition> transitions(uint32_t state) const { return transitions(root(), state); }
    static std::span<const Transition> transitions(const StateMachineNode& machine, uint32_t state);

    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    std::optional<uint32_t> findTrigger(const std::string& name) const;

private:
    std::vector<AnimNode> m_nodes;
    std::unordered_map<std::string, uint32_t> m_triggerNames;
};

// Drives an AnimationBlend from a graph: runs the root machine, consumes triggers and fires transitions, and plays
// each entered state in a blend group with the transition's fade. It keeps no fades or weights of its own.
class AnimGraphInstance {
public:
    // Resolves each clip node's clip name to an index into clips. Throws std::invalid_argument if a clip is missing,
    // or if a root state plays a nested machine, which isn't supported yet.
    AnimGraphInstance(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips);

    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    // Advances the root machine and fires at most one transition, playing its target in group (the blend's root by
    // default). The first update plays the entry state without a fade. Call before blend.step() so a state entered
    // this update already advances by deltaTime. Returns the transition that fired, if any.
    const AnimGraph::Transition* update(float deltaTime);

    // The root machine's current state.
    uint32_t currentStateIndex() const { return m_current; }
    float stateTime() const { return m_stateTime; }
    // Clip of the root machine's current state, kNone if there are no states.
    uint32_t currentClip() const;

    const AnimGraph& definition() const { return *m_definition; }
    std::vector<NodeTransform>* computePose(const Skeleton& skeleton, const std::vector<AssetPtr<AnimationClip>>& clips);

private:
    void writeTrigger(const std::string& name, uint8_t value);
    const AnimGraph::Transition* findTransition();
    void enterState(uint32_t state, float fadeDuration);

    AssetPtr<AnimGraph> m_definition;
    std::vector<uint32_t> m_nodeClips;  // per node, kNone for non-clip nodes
    std::vector<float> m_clipDurations; // per clip
    std::vector<uint8_t> m_triggers;
    uint32_t m_current = 0;
    float m_stateTime = 0.0f;
    bool m_started = false;
};

} // namespace ailo
