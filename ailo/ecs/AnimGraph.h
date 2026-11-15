#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "animation/ExprVM.h"
#include "assets/AssetServer.h"
#include "render/Animation.h"
#include "render/Skeleton.h"

namespace ailo {

template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

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
    std::optional<expr::Bytecode> condition;
};

struct AnimTransitionDesc {
    std::string trigger;
    std::string condition;
    float fadeDuration = 0.0f;
    float exitTime = 0.0f;
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

// Nodes are stored depth-first in preorder: the root machine is node 0, and every machine is followed by the
// subtrees of its states, in state order. Each node is reached from exactly one state.
class AnimGraph {
public:
    static constexpr uint16_t kNone = kAnimNone;
    static constexpr NodeIndex kRoot { 0 };
    using State = AnimState;
    using Transition = AnimTransition;

    class Builder;

    class MachineBuilder {
    public:
        using TransitionDesc = AnimTransitionDesc;

        MachineBuilder& state(std::string clip, bool looping = true, float rate = 1.0f);
        MachineBuilder& state(MachineBuilder machine);
        MachineBuilder& transition(uint16_t from, uint16_t to, TransitionDesc desc = {});
        MachineBuilder& entryState(uint16_t state);

    private:
        friend class Builder;

        struct StateDesc {
            ClipNode clip;
            uint16_t machine = kAnimNone; // into m_machines when the state is a nested machine
        };

        struct TransitionEntry {
            uint16_t from;
            uint16_t to;
            TransitionDesc desc;
        };

        void build(AnimGraph& graph, NodeIndex self, std::span<const std::string_view> parameters) const;

        std::vector<StateDesc> m_states;
        std::vector<MachineBuilder> m_machines;
        std::vector<TransitionEntry> m_transitions;
        uint16_t m_entryState = 0;
    };

    class Builder {
    public:
        Builder& parameter(std::string name, float defaultValue = 0.0f);
        Builder& root(MachineBuilder machine);

        // Throws std::invalid_argument if a machine has no states, a transition or entry state is out of range, a
        // condition doesn't compile, or a parameter is declared twice.
        AnimGraph build() const;

    private:
        struct ParameterDesc {
            std::string name;
            float defaultValue;
        };

        std::vector<ParameterDesc> m_parameters;
        MachineBuilder m_root;
    };

    AnimGraph(AnimGraph&&) noexcept = default;
    AnimGraph& operator=(AnimGraph&&) noexcept = default;

    const AnimNode& node(NodeIndex id) const { return m_nodes[id.value]; }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }

    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    std::optional<uint16_t> findTrigger(const std::string& name) const;

    uint32_t parameterCount() const { return static_cast<uint32_t>(m_parameters.size()); }
    std::optional<uint16_t> findParameter(const std::string& name) const;
    const std::string& parameterName(uint16_t parameter) const { return m_parameters[parameter].name; }
    float parameterDefault(uint16_t parameter) const { return m_parameters[parameter].defaultValue; }

private:
    struct Parameter {
        std::string name;
        float defaultValue;
    };

    AnimGraph() = default;

    NodeIndex addNode(AnimNode node);
    uint16_t addTrigger(const std::string& name);
    void addParameter(const std::string& name, float defaultValue);

    std::vector<AnimNode> m_nodes;
    std::unordered_map<std::string, uint16_t> m_triggerNames;
    std::vector<Parameter> m_parameters;
    std::unordered_map<std::string, uint16_t> m_parameterIndices;
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

// m_tree mirrors the graph's nodes one to one, so tree and graph share NodeIndex.
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
        NodeIndex fromNode;
        NodeIndex toNode;
        float time = 0.0f;
        float duration = 0.0f;

        float alpha() const { return duration <= 0.0f ? 1.0f : std::min(time / duration, 1.0f); }
    };

    struct StateMachine {
        uint16_t currentState = 0;
        NodeIndex currentNode;
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

    void setFloat(const std::string& name, float value);
    void setFloat(uint16_t parameter, float value) { m_parameters[parameter] = value; }
    float getFloat(const std::string& name) const;
    float getFloat(uint16_t parameter) const { return m_parameters[parameter]; }

    void update(float dt);
    void evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    // The state index the machine is in, or is fading into.
    uint16_t currentState(NodeIndex machine = AnimGraph::kRoot) const;

private:
    void resetNode(NodeIndex index);

    static void updateClip(Clip& clip, float dt);
    void updateMachine(NodeIndex index, float dt);
    void evaluateNode(NodeIndex index, const Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    AssetPtr<AnimGraph> m_definition;
    std::vector<Node> m_tree;
    std::vector<uint8_t> m_triggers;
    std::vector<float> m_parameters;
};

} // namespace ailo
