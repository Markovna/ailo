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

struct NodeIndex {
    static constexpr uint16_t kInvalid = std::numeric_limits<uint16_t>::max();

    uint16_t value = kInvalid;

    explicit operator uint16_t() const { return value; }

    constexpr bool valid() const { return value != kInvalid; }
    friend constexpr bool operator==(NodeIndex, NodeIndex) = default;
};

struct AnimState {
    NodeIndex   node;
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

    std::span<const AnimTransition> transitionsFrom(uint16_t state) const {
        const auto& s = states[state];
        return std::span(transitions).subspan(s.firstTransition, s.transitionCount);
    }
};

struct AnimNode : std::variant<ClipNode, StateMachineNode> {
    using std::variant<ClipNode, StateMachineNode>::variant;
};

class AnimGraph {
public:
    static constexpr uint16_t kNone = kAnimNone;
    static constexpr NodeIndex kRoot { 0 };
    using State = AnimState;
    using Transition = AnimTransition;

    class Builder {
    public:
        Builder& state(std::string clip, bool looping = true, float rate = 1.0f);
        Builder& state(Builder machine);
        Builder& transition(uint16_t from, uint16_t to, std::string trigger = {}, float fadeDuration = 0.0f,
                            float exitTime = 0.0f);
        Builder& entryState(uint16_t state);

        // Throws std::invalid_argument if a machine has no states or a transition or entry state is out of range.
        AnimGraph build() const;

    private:
        struct StateDesc {
            ClipNode clip;
            uint16_t machine = kAnimNone; // into m_machines when the state is a nested machine
        };

        struct TransitionDesc {
            uint16_t from;
            uint16_t to;
            std::string trigger;
            float fadeDuration;
            float exitTime;
        };

        void build(AnimGraph& graph, NodeIndex self) const;

        std::vector<StateDesc> m_states;
        std::vector<Builder> m_machines;
        std::vector<TransitionDesc> m_transitions;
        uint16_t m_entryState = 0;
    };

    AnimGraph(AnimGraph&&) noexcept = default;
    AnimGraph& operator=(AnimGraph&&) noexcept = default;

    const AnimNode& node(NodeIndex id) const { return m_nodes[id.value]; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }

    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    std::optional<uint16_t> findTrigger(const std::string& name) const;

private:
    AnimGraph() = default;

    NodeIndex addNode(AnimNode node);
    uint16_t addTrigger(const std::string& name);

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
    static constexpr uint16_t kNoTreeNode = NodeIndex::kInvalid;

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
        NodeIndex machine;
        uint16_t currentState = 0;
        uint16_t currentNode = 0;
        float stateTime = 0.0f; // since currentState was entered
        std::vector<Transition> m_transitionStack;
    };

    struct Node {
        std::variant<Clip, StateMachine> data;
        bool active = false;
        bool wasActive = false;
    };

public:
    AnimGraphRunner(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips);

    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    void update(float dt);
    void evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    // The state index the machine is in, or is fading into.
    uint16_t currentState(NodeIndex machine = AnimGraph::kRoot) const;

private:
    void resetNode(Node& node);

    static void updateClip(Clip& clip, float dt);
    void updateMachine(StateMachine& sm, float dt);
    void evaluateNode(uint16_t index, const Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    AssetPtr<AnimGraph> m_definition;
    std::vector<Node> m_tree;
    std::vector<uint8_t> m_triggers;
    std::vector<uint16_t> m_nodesMap; // tree index by NodeId::value, kNoTreeNode if unreachable
};

} // namespace ailo
