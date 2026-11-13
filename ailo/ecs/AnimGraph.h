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

inline constexpr uint16_t kAnimNone = std::numeric_limits<uint16_t>::max();

// Index of a node in an AnimGraph.
struct NodeId {
    static constexpr uint16_t kInvalid = std::numeric_limits<uint16_t>::max();

    uint16_t value = kInvalid;

    constexpr bool valid() const { return value != kInvalid; }
    friend constexpr bool operator==(NodeId, NodeId) = default;
};

struct AnimState {
    NodeId   node;
    uint16_t firstTransition = 0;         // into the machine's transitions
    uint16_t transitionCount = 0;
};

struct AnimTransition {
    uint16_t to           = 0;            // state of the same machine
    uint16_t trigger      = kAnimNone;
    // Seconds the from-state must have been active before the transition can fire.
    float    exitTime     = 0.0f;
    float    fadeDuration = 0.0f;
};

// State and transition indices are local to their machine.
struct StateMachineNode {
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions; // grouped by from-state
    uint16_t entryState = 0;
};

using AnimNode = std::variant<ClipNode, StateMachineNode>;

// Immutable once shared: a tree of nodes in a flat array that refer to each other by NodeId. kRoot is the root
// machine. Each node may be the state of at most one machine.
class AnimGraph {
public:
    static constexpr uint16_t kNone = kAnimNone;
    static constexpr NodeId kRoot { 0 };
    using State = AnimState;
    using Transition = AnimTransition;

    AnimGraph();

    // Returns a new empty machine, to be used as a state with addMachineState.
    NodeId addMachine();

    // Return the state index within the machine.
    uint16_t addState(std::string clip, bool looping = true);
    uint16_t addState(NodeId machine, std::string clip, bool looping = true);
    uint16_t addMachineState(NodeId machine, NodeId child);

    uint16_t addTrigger(const std::string& name);
    void addTransition(uint16_t from, uint16_t to, const std::string& trigger = {}, float fadeDuration = 0.0f,
                       float exitTime = 0.0f);
    void addTransition(NodeId machine, uint16_t from, uint16_t to, const std::string& trigger = {},
                       float fadeDuration = 0.0f, float exitTime = 0.0f);
    void setEntryState(NodeId machine, uint16_t state);

    const AnimNode& node(NodeId id) const { return m_nodes[id.value]; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
    const StateMachineNode& root() const { return machine(kRoot); }
    const StateMachineNode& machine(NodeId id) const { return std::get<StateMachineNode>(m_nodes[id.value]); }

    // The root machine's states.
    uint32_t stateCount() const { return static_cast<uint32_t>(root().states.size()); }
    const State& state(uint16_t index) const { return root().states[index]; }
    std::span<const Transition> transitions(uint16_t state) const { return transitions(root(), state); }
    static std::span<const Transition> transitions(const StateMachineNode& machine, uint16_t state);

    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    std::optional<uint16_t> findTrigger(const std::string& name) const;

private:
    StateMachineNode& mutableMachine(NodeId id) { return std::get<StateMachineNode>(m_nodes[id.value]); }
    NodeId addNode(AnimNode node);
    uint16_t addStateNode(NodeId machine, NodeId node);

    std::vector<AnimNode> m_nodes;
    std::unordered_map<std::string, uint16_t> m_triggerNames;
};

struct PosePool {
    std::vector<std::vector<NodeTransform>> free;

    std::vector<NodeTransform> acquire() {
        if (free.empty()) {
            return {};
        }
        auto pose = std::move(free.back());
        free.pop_back();
        return pose;
    }

    void release(std::vector<NodeTransform>&& pose) { free.push_back(std::move(pose)); }
};

class AnimGraphRunner {
private:
    // The tree has at most one node per graph node, so its indexes fit below NodeId::kInvalid.
    static constexpr uint16_t kNoTreeNode = NodeId::kInvalid;

    struct Clip {
        AssetPtr<AnimationClip> clip;
        float rate = 1.0f;
        bool looping = true;
        float time = 0.0f;
        float duration = 0.0f;
    };

    struct Transition {
        uint16_t fromNode;
        uint16_t toNode;
        float time = 0.0f;
        float duration = 0.0f;

        float alpha() const { return duration <= 0.0f ? 1.0f : std::min(time / duration, 1.0f); }
    };

    struct StateMachine {
        NodeId machine;
        uint16_t currentState = 0;
        uint16_t currentNode = 0;
        float stateTime = 0.0f; // since currentState was entered
        std::vector<Transition> m_transitionStack;
    };

    struct Node {
        std::variant<Clip, StateMachine> data;
        uint8_t depth = 0;
        bool active = 0.0f;
        bool wasActive = 0.0f;
    };

public:
    AnimGraphRunner(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips);

    // A set trigger stays set until a transition that uses it fires.
    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    void update(float dt);
    void evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

private:
    static void updateClip(Clip& clip, float dt);
    // Picks a transition and writes the children's local weights.
    void updateMachine(StateMachine& sm, float dt);
    void evaluateNode(uint16_t index, const Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    AssetPtr<AnimGraph> m_definition;
    std::vector<Node> m_tree;
    std::vector<uint8_t> m_triggers;
    std::vector<uint16_t> m_nodesMap; // tree index by NodeId::value, kNoTreeNode if unreachable
};

} // namespace ailo
