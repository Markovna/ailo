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
        const auto& desc = m_states[i];
        if (desc.machine == kNone) {
            machine.states[i].node = graph.addNode(desc.clip);
        } else {
            const NodeIndex child = graph.addNode(StateMachineNode {});
            m_machines[desc.machine].build(graph, child, parameters);
            machine.states[i].node = child;
        }
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
            return stack.empty() ? syncTrack(sm.currentNode) : stack.front().syncTrack;
        },
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

        const SyncState source = stack.empty() ? syncState(sm.currentNode) : stack.front().syncState();
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
            .syncTrack = SyncTrack(*source.track, *syncState(to).track, 0.0f),
        };
        const SyncState target = syncState(to);
        fade.syncDuration = SyncTrack::blendedDuration(source.duration, source.track->eventCount(), target.duration,
                                                       target.track->eventCount(), fade.syncTrack.eventCount(), 0.0f);
        fade.syncPercent = fade.syncTrack.percent(sourceTime);

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
        node.duration = stack.front().syncDuration;
        node.percent = stack.front().syncPercent;
    }
}

void AnimGraphRunner::updateFade(StateMachine& sm, size_t index, float dt, const SyncTimeRange* range) {
    auto& stack = sm.m_transitionStack;
    Transition& fade = stack[index];
    const Transition* below = index + 1 < stack.size() ? &stack[index + 1] : nullptr;

    const auto sourceState = [&] { return below ? below->syncState() : syncState(fade.fromNode); };
    const auto updateSource = [&](const SyncTimeRange* sideRange) {
        if (below) {
            updateFade(sm, index + 1, dt, sideRange);
        } else {
            updateNode(fade.fromNode, dt, sideRange);
        }
    };
    const auto updateSides = [&](const SyncTimeRange* sideRange) {
        updateSource(sideRange);
        updateNode(fade.toNode, dt, sideRange);
    };
    const auto blend = [&] {
        const SyncState from = sourceState();
        const SyncState to = syncState(fade.toNode);
        fade.syncTrack = SyncTrack(*from.track, *to.track, fade.alpha());
        fade.syncDuration = SyncTrack::blendedDuration(from.duration, from.track->eventCount(), to.duration,
                                                       to.track->eventCount(), fade.syncTrack.eventCount(),
                                                       fade.alpha());
    };

    if (!fade.synchronized) {
        updateNode(fade.toNode, dt, range);
        updateSource(nullptr);
        const SyncState target = syncState(fade.toNode);
        fade.syncTrack = *target.track;
        fade.syncDuration = target.duration;
        fade.syncPercent = target.percent;
    } else if (range) {
        updateSides(range);
        blend();
        fade.syncPercent = fade.syncTrack.percent(range->end);
    } else {
        float end = fade.syncPercent + (fade.syncDuration > 0.0f ? dt / fade.syncDuration : 0.0f);
        end -= std::floor(end);
        const SyncTimeRange fadeRange { fade.syncTrack.time(fade.syncPercent), fade.syncTrack.time(end) };
        updateSides(&fadeRange);
        blend();
        fade.syncPercent = fade.syncTrack.percent(fadeRange.end);
    }
}

} // namespace ailo
