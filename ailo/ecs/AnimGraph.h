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
#include "animation/SyncTrack.h"
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
    bool     synchronized = false;
    std::optional<expr::Bytecode> condition;
};

struct AnimTransitionDesc {
    std::string trigger;
    std::string condition;
    float fadeDuration = 0.0f;
    float exitTime = 0.0f;
    bool synchronized = false;
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

struct BlendSample1D {
    NodeIndex node;
    float position = 0.0f;
};

struct BlendSpace1DNode {
    // Samples lo and hi, by index into samples, with hi weighing weight over lo.
    struct Segment {
        uint16_t lo;
        uint16_t hi;
        float weight;
    };

    uint16_t parameter = kAnimNone;
    float rate = 1.0f;
    std::vector<BlendSample1D> samples; // sorted by position

    // Below the first sample, at or above the last, or NaN, the value plays only the nearest end sample.
    Segment segment(float value) const;
};

struct AnimNode : std::variant<ClipNode, StateMachineNode, BlendSpace1DNode> {
    using std::variant<ClipNode, StateMachineNode, BlendSpace1DNode>::variant;
};

// The root machine is node 0. Each node is reached from exactly one state or sample.
class AnimGraph {
public:
    static constexpr uint16_t kNone = kAnimNone;
    static constexpr NodeIndex kRoot { 0 };
    using State = AnimState;
    using Transition = AnimTransition;

    class Builder;
    class MachineBuilder;

private:
    // A state or sample: a clip, or a nested machine or blend space, by index into its builder's.
    struct ChildDesc {
        ClipNode clip;
        uint16_t machine = kAnimNone;
        uint16_t blendSpace = kAnimNone;
    };

public:
    class BlendSpace1DBuilder {
    public:
        explicit BlendSpace1DBuilder(std::string parameter, float rate = 1.0f);

        // A sample plays at the blend space's pace, so a clip sample has no rate of its own.
        BlendSpace1DBuilder& sample(float position, std::string clip, bool looping = true);
        BlendSpace1DBuilder& sample(float position, MachineBuilder machine);
        BlendSpace1DBuilder& sample(float position, BlendSpace1DBuilder blendSpace);

    private:
        friend class AnimGraph;

        struct SampleDesc {
            float position;
            ChildDesc node;
        };

        void build(AnimGraph& graph, NodeIndex self, std::span<const std::string_view> parameters) const;

        std::string m_parameter;
        float m_rate;
        std::vector<SampleDesc> m_samples;
        std::vector<MachineBuilder> m_machines;
        std::vector<BlendSpace1DBuilder> m_blendSpaces;
    };

    class MachineBuilder {
    public:
        using TransitionDesc = AnimTransitionDesc;

        MachineBuilder& state(std::string clip, bool looping = true, float rate = 1.0f);
        MachineBuilder& state(MachineBuilder machine);
        MachineBuilder& state(BlendSpace1DBuilder blendSpace);
        MachineBuilder& transition(uint16_t from, uint16_t to, TransitionDesc desc = {});
        MachineBuilder& entryState(uint16_t state);

    private:
        friend class AnimGraph;
        friend class Builder;

        struct TransitionEntry {
            uint16_t from;
            uint16_t to;
            TransitionDesc desc;
        };

        void build(AnimGraph& graph, NodeIndex self, std::span<const std::string_view> parameters) const;

        std::vector<ChildDesc> m_states;
        std::vector<MachineBuilder> m_machines;
        std::vector<BlendSpace1DBuilder> m_blendSpaces;
        std::vector<TransitionEntry> m_transitions;
        uint16_t m_entryState = 0;
    };

    class Builder {
    public:
        Builder& parameter(std::string name, float defaultValue = 0.0f);
        Builder& root(MachineBuilder machine);

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
    // Adds the child's node, then its subtree.
    NodeIndex addChild(const ChildDesc& child, std::span<const MachineBuilder> machines,
                       std::span<const BlendSpace1DBuilder> blendSpaces, std::span<const std::string_view> parameters);
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

class AnimGraphRunner {
private:
    struct Clip {
        AssetPtr<AnimationClip> clip;
        float rate = 1.0f;
        bool looping = true;

        const SyncTrack& syncTrack() const;
    };

    // Where a node or a fade is on its sync track.
    struct SyncState {
        const SyncTrack* track;
        float duration;
        float percent;

        SyncTime time() const { return track->time(percent); }
    };

    struct SyncedBlend {
        SyncTrack track;
        float duration = 0.0f;
        float percent = 0.0f;

        SyncState state() const { return { &track, duration, percent }; }

        SyncTimeRange advance(float dt, const SyncTimeRange* range) const;

        void blend(const SyncState& a, const SyncState& b, float weight, SyncTime time);
    };

    // A fade's source is the fade below it on the stack, or fromNode for the oldest one. A synchronized fade's sync track
    // blends its source's and toNode's at alpha; any other fade's is toNode's.
    struct Transition {
        NodeIndex fromNode;
        NodeIndex toNode;
        float time = 0.0f;
        float duration = 0.0f;
        bool synchronized = false;
        SyncedBlend sync;

        float alpha() const { return duration <= 0.0f ? 1.0f : std::min(time / duration, 1.0f); }
    };

    struct StateMachine {
        uint16_t currentState = 0;
        NodeIndex currentNode;
        float stateTime = 0.0f; // since currentState was entered
        std::vector<Transition> m_transitionStack;
    };

    struct BlendSpace {
        uint16_t lo = 0;
        uint16_t hi = 0;
        float weight = 0.0f;
        SyncedBlend sync;
    };

    // A machine's duration and percent are those of the fade it is in, or of its state, and a blend space's those of
    // its blend, copied up as they update.
    struct Node {
        std::variant<Clip, StateMachine, BlendSpace> data;
        float duration = 0.0f;          // seconds per loop
        float percent = 0.0f;
        bool updated = false;           // this frame
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
    void startNode(NodeIndex index, const SyncTime* start = nullptr);

    const SyncTrack& syncTrack(NodeIndex index) const;
    SyncState syncState(NodeIndex index) const;

    void updateNode(NodeIndex index, float dt, const SyncTimeRange* range);
    static void updateClip(Node& node, const Clip& clip, float dt);
    static void updateClipSynchronized(Node& node, const Clip& clip, const SyncTimeRange& range);
    void updateMachine(NodeIndex index, float dt, const SyncTimeRange* range);
    // Updates the fade at index on the machine's stack, and the fades below it.
    void updateFade(StateMachine& sm, size_t index, float dt, const SyncTimeRange* range);
    void updateBlendSpace(NodeIndex index, float dt, const SyncTimeRange* range);
    void evaluateNode(NodeIndex index, const Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out);

    AssetPtr<AnimGraph> m_definition;
    std::vector<Node> m_tree;
    std::vector<uint8_t> m_triggers;
    std::vector<float> m_parameters;
};

} // namespace ailo
