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
// machine. Each node may be the state of at most one machine.
class AnimGraph {
public:
    static constexpr uint32_t kNone = kAnimNone;
    using State = AnimState;
    using Transition = AnimTransition;

    AnimGraph();

    // Returns the node index of a new empty machine, to be used as a state with addMachineState.
    uint32_t addMachine();

    // Return the state index within the machine.
    uint32_t addState(std::string clip, bool looping = true);
    uint32_t addState(uint32_t machine, std::string clip, bool looping = true);
    uint32_t addMachineState(uint32_t machine, uint32_t child);

    uint32_t addTrigger(const std::string& name);
    void addTransition(uint32_t from, uint32_t to, const std::string& trigger = {}, float fadeDuration = 0.0f,
                       float exitTime = 0.0f);
    void addTransition(uint32_t machine, uint32_t from, uint32_t to, const std::string& trigger = {},
                       float fadeDuration = 0.0f, float exitTime = 0.0f);
    void setEntryState(uint32_t machine, uint32_t state);

    const AnimNode& node(uint32_t index) const { return m_nodes[index]; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
    const StateMachineNode& root() const { return machine(0); }
    const StateMachineNode& machine(uint32_t node) const { return std::get<StateMachineNode>(m_nodes[node]); }

    // The root machine's states.
    uint32_t stateCount() const { return static_cast<uint32_t>(root().states.size()); }
    const State& state(uint32_t index) const { return root().states[index]; }
    std::span<const Transition> transitions(uint32_t state) const { return transitions(root(), state); }
    static std::span<const Transition> transitions(const StateMachineNode& machine, uint32_t state);

    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    std::optional<uint32_t> findTrigger(const std::string& name) const;

private:
    StateMachineNode& mutableMachine(uint32_t node) { return std::get<StateMachineNode>(m_nodes[node]); }
    uint32_t addStateNode(uint32_t machine, uint32_t node);

    std::vector<AnimNode> m_nodes;
    std::unordered_map<std::string, uint32_t> m_triggerNames;
};

// Fade in and fade out ramp to 1 independently over the time left on each, so a fade can restart from any point.
struct AnimCrossfade {
    float fadeIn      = 0.0f;
    float fadeOut     = 0.0f;
    float fadeInLeft  = 0.0f;
    float fadeOutLeft = 0.0f;

    float blend() const { return std::max(0.0f, fadeIn - fadeOut); }
    void step(float deltaTime);
    // Continues from the current blend.
    void startFadeIn(float duration);
    // Starts fading out, or shortens a fade out already running to at most duration.
    void fadeOutWithin(float duration);
};

// Runs a graph on per-instance runtime nodes, one per graph node, laid out in a flat preorder array. update() walks
// it forward: each node steps its crossfade and takes its weight from its parent, and each machine fires at most one
// transition, starting the fades of its states before they are reached. evaluate() descends from the root through
// the nodes with a non-zero blend and blends their poses, normalizing within each machine.
class AnimGraphInstance {
public:
    // Resolves each clip node's clip name to an index into clips. Throws std::invalid_argument if a clip is missing
    // or the graph isn't a tree rooted at node 0.
    AnimGraphInstance(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips);

    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    void update(float deltaTime);
    // clips must be the ones the instance was built with. Returns false, leaving out alone, if the root has no states.
    bool evaluate(const Skeleton& skeleton, std::span<const AssetPtr<AnimationClip>> clips,
                  std::vector<NodeTransform>& out);

    const AnimGraph& definition() const { return *m_definition; }

    uint32_t currentState(uint32_t machineNode = 0) const;
    float stateTime(uint32_t machineNode = 0) const;
    // Product of the blends from the root down to node. Not normalized: blends overlap while fades do.
    float weight(uint32_t node) const;
    float blend(uint32_t node) const;
    float clipTime(uint32_t clipNode) const;

private:
    struct RuntimeNode {
        uint32_t graphNode   = 0;
        uint32_t subtreeSize = 1;
        uint32_t depth       = 0;
        uint32_t data        = 0;     // into m_clips or m_machines
        uint32_t parent      = kAnimNone;
        uint32_t state       = 0;     // index in the parent machine
        bool     machine     = false;
        bool     active      = false; // on the chain of current states from the root
        float    weight      = 0.0f;
        AnimCrossfade fade;
    };

    struct ClipRuntime {
        uint32_t clip     = 0;
        float    time     = 0.0f;
        float    duration = 0.0f;
        float    rate     = 1.0f;
        bool     looping  = true;
    };

    struct MachineRuntime {
        uint32_t firstStateNode = 0; // into m_stateNodes
        uint32_t stateCount     = 0;
        uint32_t current        = 0;
        float    stateTime      = 0.0f;
    };

    uint32_t build(uint32_t graphNode, uint32_t parent, uint32_t state, uint32_t depth, std::vector<bool>& visited,
                   std::span<const AssetPtr<AnimationClip>> clips);
    void reset(uint32_t node);
    void updateMachine(const RuntimeNode& node, float deltaTime);
    const AnimTransition* findTransition(const StateMachineNode& machine, const MachineRuntime& runtime);
    void evaluateNode(uint32_t index, const Skeleton& skeleton, std::span<const AssetPtr<AnimationClip>> clips,
                      std::vector<NodeTransform>& out);
    RuntimeNode& stateNode(const MachineRuntime& machine, uint32_t state) {
        return m_nodes[m_stateNodes[machine.firstStateNode + state]];
    }
    const MachineRuntime& machineAt(uint32_t graphNode) const;
    void writeTrigger(const std::string& name, uint8_t value);

    AssetPtr<AnimGraph> m_definition;
    std::vector<RuntimeNode> m_nodes;     // preorder
    std::vector<uint32_t> m_runtimeIndex; // per graph node
    std::vector<ClipRuntime> m_clips;
    std::vector<MachineRuntime> m_machines;
    std::vector<uint32_t> m_stateNodes;   // runtime index of each machine's states
    std::vector<std::vector<NodeTransform>> m_scratch; // per depth
    std::vector<uint8_t> m_triggers;
};

} // namespace ailo
