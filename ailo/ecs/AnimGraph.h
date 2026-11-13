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
    struct Clip {
        AssetPtr<AnimationClip> clip;
        float rate = 1.0f;
        bool looping = true;
        float time = 0.0f;
        float duration = 0.0f;
    };

    struct Transition {
        uint32_t fromNode;
        uint32_t toNode;
        float time = 0.0f;
        float duration = 0.0f;

        float alpha() const { return duration <= 0.0f ? 1.0f : std::min(time / duration, 1.0f); }
    };

    struct StateMachine {
        const StateMachineNode* sm;
        uint32_t currentState = 0;
        uint32_t currentNode = 0;
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
    void evaluateNode(uint32_t index, const Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    AssetPtr<AnimGraph> m_definition;
    std::vector<Node> m_tree;
    std::vector<uint8_t> m_triggers;
    std::vector<uint32_t> m_nodesMap; // maps definition nodes to tree indexes
};

} // namespace ailo
