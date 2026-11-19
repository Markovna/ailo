#include "AnimGraph.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

#include "PoseOps.h"
#include "utils/Utils.h"

namespace ailo {

namespace {

constexpr uint16_t kNone = AnimGraph::kNone;

const SyncTrack kDefaultSyncTrack;

}

BlendSpace1DNode::Segment BlendSpace1DNode::segment(float value) const {
    if (!(value > samples.front().position)) {
        return { 0, 0, 0.0f };
    }
    const auto last = static_cast<uint16_t>(samples.size() - 1);
    if (value >= samples[last].position) {
        return { last, last, 0.0f };
    }

    uint16_t hi = 1;
    while (samples[hi].position <= value) {
        ++hi;
    }
    const auto lo = static_cast<uint16_t>(hi - 1);
    const float weight = (value - samples[lo].position) / (samples[hi].position - samples[lo].position);
    return { lo, hi, weight };
}

const SyncTrack& AnimGraphRunner::Clip::syncTrack() const {
    const AnimationClip* asset = clip.get();
    return asset ? asset->syncTrack : kDefaultSyncTrack;
}

AnimGraph::MachineBuilder& AnimGraph::MachineBuilder::state(std::string clip, bool looping, float rate) {
    m_states.push_back({ .clip = { .clip = std::move(clip), .rate = rate, .looping = looping } });
    return *this;
}

AnimGraph::MachineBuilder& AnimGraph::MachineBuilder::state(MachineBuilder machine) {
    m_states.push_back({ .machine = static_cast<uint16_t>(m_machines.size()) });
    m_machines.push_back(std::move(machine));
    return *this;
}

AnimGraph::MachineBuilder& AnimGraph::MachineBuilder::state(BlendSpace1DBuilder blendSpace) {
    m_states.push_back({ .blendSpace = static_cast<uint16_t>(m_blendSpaces.size()) });
    m_blendSpaces.push_back(std::move(blendSpace));
    return *this;
}

AnimGraph::BlendSpace1DBuilder::BlendSpace1DBuilder(std::string parameter, float rate)
    : m_parameter(std::move(parameter)), m_rate(rate) {}

AnimGraph::BlendSpace1DBuilder& AnimGraph::BlendSpace1DBuilder::sample(float position, std::string clip,
                                                                       bool looping) {
    m_samples.push_back({ .position = position, .node = { .clip = { .clip = std::move(clip), .looping = looping } } });
    return *this;
}

AnimGraph::BlendSpace1DBuilder& AnimGraph::BlendSpace1DBuilder::sample(float position, MachineBuilder machine) {
    m_samples.push_back({ .position = position, .node = { .machine = static_cast<uint16_t>(m_machines.size()) } });
    m_machines.push_back(std::move(machine));
    return *this;
}

AnimGraph::BlendSpace1DBuilder& AnimGraph::BlendSpace1DBuilder::sample(float position,
                                                                       BlendSpace1DBuilder blendSpace) {
    m_samples.push_back(
        { .position = position, .node = { .blendSpace = static_cast<uint16_t>(m_blendSpaces.size()) } });
    m_blendSpaces.push_back(std::move(blendSpace));
    return *this;
}

void AnimGraph::BlendSpace1DBuilder::build(AnimGraph& graph, NodeIndex self,
                                           std::span<const std::string_view> parameters) const {
    if (m_samples.empty()) {
        throw std::invalid_argument(std::format("animation graph: blend space {} has no samples", self.value));
    }
    if (m_samples.size() >= kNone) {
        throw std::invalid_argument(std::format("animation graph: blend space {} is too large", self.value));
    }
    const auto parameter = graph.findParameter(m_parameter);
    if (!parameter) {
        throw std::invalid_argument(std::format("animation graph: blend space {} reads undeclared parameter '{}'",
                                                self.value, m_parameter));
    }

    std::vector<const SampleDesc*> sorted;
    sorted.reserve(m_samples.size());
    for (const auto& sample : m_samples) {
        if (!std::isfinite(sample.position)) {
            throw std::invalid_argument(
                std::format("animation graph: blend space {} has a sample at a non-finite position", self.value));
        }
        sorted.push_back(&sample);
    }
    std::ranges::sort(sorted, {}, &SampleDesc::position);
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i]->position == sorted[i - 1]->position) {
            throw std::invalid_argument(std::format("animation graph: two samples of blend space {} are at {}",
                                                    self.value, sorted[i]->position));
        }
    }

    BlendSpace1DNode blendSpace { .parameter = *parameter, .rate = m_rate };
    blendSpace.samples.reserve(sorted.size());
    for (const SampleDesc* sample : sorted) {
        blendSpace.samples.push_back({
            .node = graph.addChild(sample->node, m_machines, m_blendSpaces, parameters),
            .position = sample->position,
        });
    }
    graph.m_nodes[self.value] = std::move(blendSpace);
}

NodeIndex AnimGraph::addChild(const ChildDesc& child, std::span<const MachineBuilder> machines,
                              std::span<const BlendSpace1DBuilder> blendSpaces,
                              std::span<const std::string_view> parameters) {
    if (child.machine != kNone) {
        const NodeIndex node = addNode(StateMachineNode {});
        machines[child.machine].build(*this, node, parameters);
        return node;
    }
    if (child.blendSpace != kNone) {
        const NodeIndex node = addNode(BlendSpace1DNode {});
        blendSpaces[child.blendSpace].build(*this, node, parameters);
        return node;
    }
    return addNode(child.clip);
}

AnimGraph::MachineBuilder& AnimGraph::MachineBuilder::transition(uint16_t from, uint16_t to, TransitionDesc desc) {
    m_transitions.push_back({ .from = from, .to = to, .desc = std::move(desc) });
    return *this;
}

AnimGraph::MachineBuilder& AnimGraph::MachineBuilder::entryState(uint16_t state) {
    m_entryState = state;
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::parameter(std::string name, float defaultValue) {
    m_parameters.push_back({ .name = std::move(name), .defaultValue = defaultValue });
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::root(MachineBuilder machine) {
    m_root = std::move(machine);
    return *this;
}

AnimGraph AnimGraph::Builder::build() const {
    AnimGraph graph;
    for (const auto& parameter : m_parameters) {
        graph.addParameter(parameter.name, parameter.defaultValue);
    }

    std::vector<std::string_view> parameters;
    parameters.reserve(graph.m_parameters.size());
    for (const auto& parameter : graph.m_parameters) {
        parameters.push_back(parameter.name);
    }

    m_root.build(graph, graph.addNode(StateMachineNode {}), parameters);
    return graph;
}

// Each machine's node is added before its states are, and a nested machine's subtree is finished before the next
// state is added, so the nodes end up in the preorder AnimGraph promises.
void AnimGraph::MachineBuilder::build(AnimGraph& graph, NodeIndex self,
                                      std::span<const std::string_view> parameters) const {
    if (m_states.empty()) {
        throw std::invalid_argument(std::format("animation graph: machine {} has no states", self.value));
    }
    if (m_states.size() >= kNone || m_transitions.size() >= kNone) {
        throw std::invalid_argument(std::format("animation graph: machine {} is too large", self.value));
    }
    if (m_entryState >= m_states.size()) {
        throw std::invalid_argument(
            std::format("animation graph: entry state {} of machine {} is out of range", m_entryState, self.value));
    }

    StateMachineNode machine { .entryState = m_entryState };
    machine.states.resize(m_states.size());
    for (size_t i = 0; i < m_states.size(); ++i) {
        machine.states[i].node = graph.addChild(m_states[i], m_machines, m_blendSpaces, parameters);
    }

    std::vector<const TransitionEntry*> sorted;
    sorted.reserve(m_transitions.size());
    for (const auto& transition : m_transitions) {
        if (transition.from >= m_states.size() || transition.to >= m_states.size()) {
            throw std::invalid_argument(std::format("animation graph: transition {} -> {} of machine {} is out of range",
                                                    transition.from, transition.to, self.value));
        }
        sorted.push_back(&transition);
    }
    std::ranges::stable_sort(sorted, {}, &TransitionEntry::from);

    machine.transitions.reserve(sorted.size());
    for (const TransitionEntry* transition : sorted) {
        const TransitionDesc& desc = transition->desc;

        std::optional<expr::Bytecode> condition;
        if (!desc.condition.empty()) {
            condition = expr::compile(desc.condition, parameters);
            if (!condition->ok()) {
                throw std::invalid_argument(
                    std::format("animation graph: condition `{}` of transition {} -> {} of machine {}: {}",
                                desc.condition, transition->from, transition->to, self.value, condition->error()));
            }
            float value = 0.0f;
            if (expr::isConstant(condition->data(), &value) && value != 0.0f) {
                condition.reset();
            }
        }

        auto& from = machine.states[transition->from];
        if (from.transitionCount == 0) {
            from.firstTransition = static_cast<uint16_t>(machine.transitions.size());
        }
        from.transitionCount++;
        machine.transitions.push_back({
            .to = transition->to,
            .trigger = graph.addTrigger(desc.trigger),
            .exitTime = desc.exitTime,
            .fadeDuration = desc.fadeDuration,
            .synchronized = desc.synchronized,
            .condition = std::move(condition),
        });
    }

    graph.m_nodes[self.value] = std::move(machine);
}

NodeIndex AnimGraph::addNode(AnimNode node) {
    if (m_nodes.size() >= NodeIndex::kInvalid) {
        throw std::invalid_argument("animation graph: too many nodes");
    }
    m_nodes.push_back(std::move(node));
    return NodeIndex { static_cast<uint16_t>(m_nodes.size() - 1) };
}

uint16_t AnimGraph::addTrigger(const std::string& name) {
    if (name.empty()) return kNone;
    if (m_triggerNames.size() >= kNone) {
        throw std::invalid_argument("animation graph: too many triggers");
    }
    return m_triggerNames.try_emplace(name, static_cast<uint16_t>(m_triggerNames.size())).first->second;
}

std::optional<uint16_t> AnimGraph::findTrigger(const std::string& name) const {
    auto it = m_triggerNames.find(name);
    if (it == m_triggerNames.end()) {
        return std::nullopt;
    }
    return it->second;
}

void AnimGraph::addParameter(const std::string& name, float defaultValue) {
    if (m_parameters.size() > expr::kMaxData) {
        throw std::invalid_argument("animation graph: too many parameters");
    }
    if (!m_parameterIndices.try_emplace(name, static_cast<uint16_t>(m_parameters.size())).second) {
        throw std::invalid_argument(std::format("animation graph: parameter '{}' is declared twice", name));
    }
    m_parameters.push_back({ .name = name, .defaultValue = defaultValue });
}

std::optional<uint16_t> AnimGraph::findParameter(const std::string& name) const {
    auto it = m_parameterIndices.find(name);
    if (it == m_parameterIndices.end()) {
        return std::nullopt;
    }
    return it->second;
}

AnimGraphRunner::AnimGraphRunner(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips)
    : m_definition(std::move(definition)) {
    assert(m_definition);
    const AnimGraph& graph = *m_definition;
    m_triggers.assign(graph.triggerCount(), 0);
    m_parameters.resize(graph.parameterCount());
    for (uint16_t i = 0; i < graph.parameterCount(); ++i) {
        m_parameters[i] = graph.parameterDefault(i);
    }
    m_tree.reserve(graph.nodeCount());

    for (uint16_t i = 0; i < graph.nodeCount(); ++i) {
        std::visit(utils::overloaded {
            [&](const ClipNode& clipNode) {
                auto it = std::ranges::find_if(clips, [&](const auto& clip) { return clip && clip->name == clipNode.clip; });
                if (it == clips.end()) {
                    throw std::invalid_argument(std::format("animation graph: no clip named '{}'", clipNode.clip));
                }
                m_tree.push_back({
                    .data = Clip { .clip = *it, .rate = clipNode.rate, .looping = clipNode.looping },
                });
            },
            [&](const StateMachineNode& machine) {
                m_tree.push_back({
                    .data = StateMachine {
                        .currentState = machine.entryState,
                        .currentNode = machine.states[machine.entryState].node,
                    },
                });
            },
            [&](const BlendSpace1DNode&) { m_tree.push_back({ .data = BlendSpace {} }); },
        }, graph.node(NodeIndex { i }));
    }

    startNode(AnimGraph::kRoot);
}

void AnimGraphRunner::setTrigger(const std::string& name) {
    const auto trigger = m_definition->findTrigger(name);
    assert(trigger.has_value());
    if (trigger) {
        m_triggers[*trigger] = 1;
    }
}

void AnimGraphRunner::resetTrigger(const std::string& name) {
    const auto trigger = m_definition->findTrigger(name);
    assert(trigger.has_value());
    if (trigger) {
        m_triggers[*trigger] = 0;
    }
}

void AnimGraphRunner::setFloat(const std::string& name, float value) {
    const auto parameter = m_definition->findParameter(name);
    assert(parameter.has_value());
    if (parameter) {
        m_parameters[*parameter] = value;
    }
}

float AnimGraphRunner::getFloat(const std::string& name) const {
    const auto parameter = m_definition->findParameter(name);
    assert(parameter.has_value());
    return parameter ? m_parameters[*parameter] : 0.0f;
}

void AnimGraphRunner::update(float dt) {
    for (auto& node : m_tree) {
        node.updated = false;
    }

    updateNode(AnimGraph::kRoot, dt, nullptr);
}

void AnimGraphRunner::updateNode(NodeIndex index, float dt, const SyncTimeRange* range) {
    auto& node = m_tree[index.value];
    if (node.updated) {
        return;
    }
    node.updated = true;

    std::visit(utils::overloaded {
        [&](const Clip& clip) {
            if (range) {
                updateClipSynchronized(node, clip, *range);
            } else {
                updateClip(node, clip, dt);
            }
        },
        [&](StateMachine&) { updateMachine(index, dt, range); },
        [&](BlendSpace&) { updateBlendSpace(index, dt, range); },
    }, node.data);
}

uint16_t AnimGraphRunner::currentState(NodeIndex machine) const {
    return std::get<StateMachine>(m_tree[machine.value].data).currentState;
}

const SyncTrack& AnimGraphRunner::syncTrack(NodeIndex index) const {
    return std::visit(utils::overloaded {
        [&](const Clip& clip) -> const SyncTrack& { return clip.syncTrack(); },
        [&](const StateMachine& sm) -> const SyncTrack& {
            const auto& stack = sm.m_transitionStack;
            return stack.empty() ? syncTrack(sm.currentNode) : stack.front().sync.track;
        },
        [&](const BlendSpace& blendSpace) -> const SyncTrack& { return blendSpace.sync.track; },
    }, m_tree[index.value].data);
}

AnimGraphRunner::SyncState AnimGraphRunner::syncState(NodeIndex index) const {
    const Node& node = m_tree[index.value];
    return { &syncTrack(index), node.duration, node.percent };
}

void AnimGraphRunner::startNode(NodeIndex index, const SyncTime* start) {
    Node& node = m_tree[index.value];
    std::visit(utils::overloaded {
        [&](const Clip& clip) {
            const AnimationClip* asset = clip.clip.get();
            node.duration = asset ? asset->duration : 0.0f;
            if (start) {
                node.percent = clip.syncTrack().percent(*start);
            } else {
                node.percent = clip.rate < 0.0f ? 1.0f : 0.0f;
            }
        },
        [&](StateMachine& sm) {
            const auto& machine = std::get<StateMachineNode>(m_definition->node(index));
            sm.currentState = machine.entryState;
            sm.currentNode = machine.states[sm.currentState].node;
            sm.stateTime = 0.0f;
            sm.m_transitionStack.clear();

            startNode(sm.currentNode, start);
            const Node& entry = m_tree[sm.currentNode.value];
            node.duration = entry.duration;
            node.percent = entry.percent;
        },
        [&](BlendSpace& blendSpace) {
            const auto& definition = std::get<BlendSpace1DNode>(m_definition->node(index));
            const auto segment = definition.segment(m_parameters[definition.parameter]);
            blendSpace.lo = segment.lo;
            blendSpace.hi = segment.hi;
            blendSpace.weight = segment.weight;

            const NodeIndex lo = definition.samples[segment.lo].node;
            const NodeIndex hi = definition.samples[segment.hi].node;
            startNode(lo, start);
            const SyncTime time = start ? *start : syncState(lo).time();
            for (const auto& sample : definition.samples) {
                if (sample.node != lo) {
                    startNode(sample.node, &time);
                }
            }
            blendSpace.sync.blend(syncState(lo), syncState(hi), segment.weight, time);
            node.duration = blendSpace.sync.duration;
            node.percent = blendSpace.sync.percent;
        },
    }, node.data);
}

void AnimGraphRunner::evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out) {
    evaluateNode(AnimGraph::kRoot, skeleton, posePool, out);
}

void AnimGraphRunner::evaluateNode(NodeIndex index, const Skeleton& skeleton, PosePool& posePool,
                                   std::vector<NodeTransform>& out) {
    const Node& node = m_tree[index.value];
    std::visit(utils::overloaded {
        [&](const Clip& clip) {
            if (const AnimationClip* asset = clip.clip.get()) {
                samplePose(node.percent * node.duration, skeleton, *asset, out);
            } else {
                out = skeleton.restPose;
            }
        },
        [&](const StateMachine& sm) {
            const auto& stack = sm.m_transitionStack;
            if (stack.empty()) {
                evaluateNode(sm.currentNode, skeleton, posePool, out);
                return;
            }

            // The stack is newest first: start from the oldest source and blend each target over it.
            evaluateNode(stack.back().fromNode, skeleton, posePool, out);

            auto pose = posePool.acquire();
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                const float alpha = it->alpha();
                if (alpha <= 0.0f) continue;
                evaluateNode(it->toNode, skeleton, posePool, pose);
                blendPose(out, pose, alpha);
            }
            posePool.release(std::move(pose));
        },
        [&](const BlendSpace& blendSpace) {
            const auto& samples = std::get<BlendSpace1DNode>(m_definition->node(index)).samples;
            evaluateNode(samples[blendSpace.lo].node, skeleton, posePool, out);
            if (blendSpace.hi == blendSpace.lo || blendSpace.weight <= 0.0f) {
                return;
            }

            auto pose = posePool.acquire();
            evaluateNode(samples[blendSpace.hi].node, skeleton, posePool, pose);
            blendPose(out, pose, blendSpace.weight);
            posePool.release(std::move(pose));
        },
    }, node.data);
}

void AnimGraphRunner::updateClip(Node& node, const Clip& clip, float dt) {
    const AnimationClip* asset = clip.clip.get();
    node.duration = asset ? asset->duration : 0.0f;
    if (node.duration <= 0.0f) return;

    float percent = node.percent + dt * clip.rate / node.duration;
    if (clip.looping) {
        percent -= std::floor(percent);
    } else {
        percent = std::clamp(percent, 0.0f, 1.0f);
    }
    node.percent = percent;
}

// The parent sets the pace, so the clip's rate is ignored, as is its duration.
void AnimGraphRunner::updateClipSynchronized(Node& node, const Clip& clip, const SyncTimeRange& range) {
    const AnimationClip* asset = clip.clip.get();
    node.duration = asset ? asset->duration : 0.0f;

    const SyncTrack& track = clip.syncTrack();
    const float start = track.percent(range.start);
    const float end = track.percent(range.end);
    const bool wrapped = end < start;
    const bool finished = clip.rate >= 0.0f && node.percent >= 1.0f;

    if (clip.looping) {
        node.percent = end;
    } else {
        node.percent = wrapped || finished ? 1.0f : end;
    }
}

void AnimGraphRunner::updateMachine(NodeIndex index, float dt, const SyncTimeRange* range) {
    auto& sm = std::get<StateMachine>(m_tree[index.value].data);
    sm.stateTime += dt;

    auto& machine = std::get<StateMachineNode>(m_definition->node(index));
    auto transitions = machine.transitionsFrom(sm.currentState);

    for (auto& transition : transitions) {
        if (sm.stateTime < transition.exitTime) continue;

        const uint16_t trigger = transition.trigger;
        if (trigger != kNone && !m_triggers[trigger]) continue;
        if (transition.condition && transition.condition->run(m_parameters) == 0.0f) continue;
        if (trigger != kNone) {
            m_triggers[trigger] = 0;
        }

        auto& states = machine.states;
        const NodeIndex to = states[transition.to].node;
        auto& stack = sm.m_transitionStack;

        const SyncState source = stack.empty() ? syncState(sm.currentNode) : stack.front().sync.state();
        const SyncTime sourceTime = source.time();
        const bool running = to == sm.currentNode || std::ranges::any_of(stack, [&](const Transition& t) {
            return t.fromNode == to || t.toNode == to;
        });
        if (!running) {
            startNode(to, transition.synchronized ? &sourceTime : nullptr);
        }
        Transition fade {
            .fromNode = sm.currentNode,
            .toNode = to,
            .duration = transition.fadeDuration,
            .synchronized = transition.synchronized,
        };
        fade.sync.blend(source, syncState(to), 0.0f, sourceTime);

        // newest first
        stack.insert(stack.begin(), std::move(fade));
        sm.currentState = transition.to;
        sm.currentNode = to;
        sm.stateTime = 0.0f;
        break;
    }

    auto& stack = sm.m_transitionStack;
    for (auto& transition : stack) {
        transition.time += dt;
    }

    stack.erase(std::ranges::find_if(stack, [](const Transition& t) { return t.alpha() >= 1.0f; }), stack.end());

    auto& node = m_tree[index.value];
    if (stack.empty()) {
        updateNode(sm.currentNode, dt, range);
        const Node& current = m_tree[sm.currentNode.value];
        node.duration = current.duration;
        node.percent = current.percent;
    } else {
        updateFade(sm, 0, dt, range);
        node.duration = stack.front().sync.duration;
        node.percent = stack.front().sync.percent;
    }
}

void AnimGraphRunner::updateFade(StateMachine& sm, size_t index, float dt, const SyncTimeRange* range) {
    auto& stack = sm.m_transitionStack;
    Transition& fade = stack[index];
    const Transition* below = index + 1 < stack.size() ? &stack[index + 1] : nullptr;

    const auto updateSource = [&](const SyncTimeRange* sideRange) {
        if (below) {
            updateFade(sm, index + 1, dt, sideRange);
        } else {
            updateNode(fade.fromNode, dt, sideRange);
        }
    };

    if (!fade.synchronized) {
        updateNode(fade.toNode, dt, range);
        updateSource(nullptr);
        const SyncState target = syncState(fade.toNode);
        fade.sync = { .track = *target.track, .duration = target.duration, .percent = target.percent };
        return;
    }

    const SyncTimeRange sideRange = fade.sync.advance(dt, range);
    updateSource(&sideRange);
    updateNode(fade.toNode, dt, &sideRange);
    const SyncState source = below ? below->sync.state() : syncState(fade.fromNode);
    fade.sync.blend(source, syncState(fade.toNode), fade.alpha(), sideRange.end);
}

// The blend is rebuilt for the parameter's new value before it advances, at the sync time it had reached, so moving the
// parameter changes the pace but never makes the samples jump.
void AnimGraphRunner::updateBlendSpace(NodeIndex index, float dt, const SyncTimeRange* range) {
    Node& node = m_tree[index.value];
    auto& blendSpace = std::get<BlendSpace>(node.data);
    const auto& definition = std::get<BlendSpace1DNode>(m_definition->node(index));
    const auto& samples = definition.samples;

    const auto segment = definition.segment(m_parameters[definition.parameter]);
    const SyncTime time = blendSpace.sync.state().time();
    blendSpace.lo = segment.lo;
    blendSpace.hi = segment.hi;
    blendSpace.weight = segment.weight;

    const NodeIndex lo = samples[segment.lo].node;
    const NodeIndex hi = samples[segment.hi].node;
    blendSpace.sync.blend(syncState(lo), syncState(hi), segment.weight, time);

    const SyncTimeRange sampleRange = blendSpace.sync.advance(dt * definition.rate, range);
    updateNode(lo, dt, &sampleRange);
    updateNode(hi, dt, &sampleRange);
    blendSpace.sync.percent = blendSpace.sync.track.percent(sampleRange.end);

    node.duration = blendSpace.sync.duration;
    node.percent = blendSpace.sync.percent;
}

SyncTimeRange AnimGraphRunner::SyncedBlend::advance(float dt, const SyncTimeRange* range) const {
    if (range) {
        return *range;
    }
    float end = percent + (duration > 0.0f ? dt / duration : 0.0f);
    end -= std::floor(end);
    return { track.time(percent), track.time(end) };
}

void AnimGraphRunner::SyncedBlend::blend(const SyncState& a, const SyncState& b, float weight, SyncTime time) {
    track = SyncTrack(*a.track, *b.track, weight);
    duration = SyncTrack::blendedDuration(a.duration, a.track->eventCount(), b.duration, b.track->eventCount(),
                                          track.eventCount(), weight);
    percent = track.percent(time);
}

} // namespace ailo
