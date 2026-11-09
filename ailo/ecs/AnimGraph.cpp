#include "AnimGraph.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <stdexcept>

#include "PoseOps.h"

namespace ailo {

namespace {

constexpr uint32_t kNone = AnimGraph::kNone;

}

AnimGraph::AnimGraph() {
    m_nodes.emplace_back(StateMachineNode {});
}

uint32_t AnimGraph::addMachine() {
    m_nodes.emplace_back(StateMachineNode {});
    return static_cast<uint32_t>(m_nodes.size() - 1);
}

uint32_t AnimGraph::addState(std::string clip, bool looping) {
    return addState(0, std::move(clip), looping);
}

uint32_t AnimGraph::addState(uint32_t machine, std::string clip, bool looping) {
    const auto node = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back(ClipNode { .clip = std::move(clip), .looping = looping });
    return addStateNode(machine, node);
}

uint32_t AnimGraph::addMachineState(uint32_t machine, uint32_t child) {
    assert(child < m_nodes.size() && std::holds_alternative<StateMachineNode>(m_nodes[child]));
    return addStateNode(machine, child);
}

uint32_t AnimGraph::addStateNode(uint32_t machineNode, uint32_t node) {
    auto& m = mutableMachine(machineNode);
    const auto index = static_cast<uint32_t>(m.states.size());
    m.states.push_back({
        .node = node,
        .firstTransition = static_cast<uint32_t>(m.transitions.size()),
    });
    return index;
}

uint32_t AnimGraph::addTrigger(const std::string& name) {
    if (name.empty()) return kNone;
    return m_triggerNames.try_emplace(name, static_cast<uint32_t>(m_triggerNames.size())).first->second;
}

void AnimGraph::addTransition(uint32_t from, uint32_t to, const std::string& trigger, float fadeDuration,
                              float exitTime) {
    addTransition(0, from, to, trigger, fadeDuration, exitTime);
}

void AnimGraph::addTransition(uint32_t machineNode, uint32_t from, uint32_t to, const std::string& trigger,
                              float fadeDuration, float exitTime) {
    auto& m = mutableMachine(machineNode);
    auto& states = m.states;
    assert(from < states.size() && to < states.size());

    auto& state = states[from];
    auto position = m.transitions.begin() + state.firstTransition + state.transitionCount;
    m.transitions.insert(position, {
        .to = to,
        .trigger = addTrigger(trigger),
        .exitTime = exitTime,
        .fadeDuration = fadeDuration,
    });
    state.transitionCount++;

    for (uint32_t i = from + 1; i < states.size(); ++i) {
        states[i].firstTransition++;
    }
}

void AnimGraph::setEntryState(uint32_t machineNode, uint32_t state) {
    auto& m = mutableMachine(machineNode);
    assert(state < m.states.size());
    m.entryState = state;
}

std::span<const AnimGraph::Transition> AnimGraph::transitions(const StateMachineNode& machine, uint32_t state) {
    const auto& s = machine.states[state];
    return std::span(machine.transitions).subspan(s.firstTransition, s.transitionCount);
}

std::optional<uint32_t> AnimGraph::findTrigger(const std::string& name) const {
    auto it = m_triggerNames.find(name);
    if (it == m_triggerNames.end()) {
        return std::nullopt;
    }
    return it->second;
}

void AnimCrossfade::step(float deltaTime) {
    auto ramp = [deltaTime](float& value, float& left) {
        if (left <= 0.0f) return;
        if (deltaTime >= left) {
            value = 1.0f;
            left = 0.0f;
        } else {
            value += (1.0f - value) * deltaTime / left;
            left -= deltaTime;
        }
    };
    ramp(fadeIn, fadeInLeft);
    ramp(fadeOut, fadeOutLeft);
}

void AnimCrossfade::startFadeIn(float duration) {
    fadeIn = blend();
    fadeOut = 0.0f;
    fadeOutLeft = 0.0f;
    fadeInLeft = duration;
    if (duration <= 0.0f) {
        fadeIn = 1.0f;
        fadeInLeft = 0.0f;
    }
}

void AnimCrossfade::fadeOutWithin(float duration) {
    const bool running = fadeOut > 0.0f || fadeOutLeft > 0.0f;
    fadeOutLeft = running ? std::min(fadeOutLeft, duration) : duration;
    if (duration <= 0.0f) {
        fadeOut = 1.0f;
        fadeOutLeft = 0.0f;
    }
}

AnimGraphInstance::AnimGraphInstance(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips)
    : m_definition(std::move(definition)) {
    assert(m_definition);
    const auto& graph = *m_definition;
    m_triggers.resize(graph.triggerCount(), 0);
    m_runtimeIndex.assign(graph.nodeCount(), kNone);

    std::vector<bool> visited(graph.nodeCount(), false);
    build(0, kNone, 0, 0, visited, clips);

    uint32_t maxDepth = 0;
    for (const auto& node : m_nodes) {
        maxDepth = std::max(maxDepth, node.depth);
    }
    m_scratch.resize(maxDepth + 1);

    auto& root = m_nodes[0];
    root.active = true;
    root.fade.fadeIn = 1.0f;
    root.weight = 1.0f;
    reset(0);
}

uint32_t AnimGraphInstance::build(uint32_t graphNode, uint32_t parent, uint32_t state, uint32_t depth,
                                  std::vector<bool>& visited, std::span<const AssetPtr<AnimationClip>> clips) {
    const auto& graph = *m_definition;
    if (graphNode >= graph.nodeCount()) {
        throw std::invalid_argument(std::format("animation graph: state refers to missing node {}", graphNode));
    }
    if (visited[graphNode]) {
        throw std::invalid_argument(std::format("animation graph: node {} is reached more than once", graphNode));
    }
    visited[graphNode] = true;

    const auto index = static_cast<uint32_t>(m_nodes.size());
    m_runtimeIndex[graphNode] = index;
    m_nodes.push_back({ .graphNode = graphNode, .depth = depth, .parent = parent, .state = state });

    if (const auto* clipNode = std::get_if<ClipNode>(&graph.node(graphNode))) {
        auto it = std::ranges::find_if(clips, [&](const auto& clip) { return clip && clip->name == clipNode->clip; });
        if (it == clips.end()) {
            throw std::invalid_argument(std::format("animation graph: no clip named '{}'", clipNode->clip));
        }
        m_nodes[index].data = static_cast<uint32_t>(m_clips.size());
        m_clips.push_back({
            .clip = static_cast<uint32_t>(it - clips.begin()),
            .duration = (*it)->duration,
            .rate = clipNode->rate,
            .looping = clipNode->looping,
        });
        return index;
    }

    const auto& machine = graph.machine(graphNode);
    const auto machineIndex = static_cast<uint32_t>(m_machines.size());
    const auto firstStateNode = static_cast<uint32_t>(m_stateNodes.size());
    m_machines.push_back({
        .firstStateNode = firstStateNode,
        .stateCount = static_cast<uint32_t>(machine.states.size()),
        .current = machine.entryState,
    });
    m_nodes[index].machine = true;
    m_nodes[index].data = machineIndex;
    m_stateNodes.resize(m_stateNodes.size() + machine.states.size());

    for (uint32_t i = 0; i < machine.states.size(); ++i) {
        m_stateNodes[firstStateNode + i] = build(machine.states[i].node, index, i, depth + 1, visited, clips);
    }
    m_nodes[index].subtreeSize = static_cast<uint32_t>(m_nodes.size()) - index;
    return index;
}

void AnimGraphInstance::reset(uint32_t index) {
    const uint32_t end = index + m_nodes[index].subtreeSize;
    for (uint32_t i = index + 1; i < end; ++i) {
        m_nodes[i].fade = {};
        m_nodes[i].weight = 0.0f;
        m_nodes[i].active = false;
    }
    for (uint32_t i = index; i < end; ++i) {
        const auto& node = m_nodes[i];
        if (!node.machine) {
            m_clips[node.data].time = 0.0f;
            continue;
        }
        auto& machine = m_machines[node.data];
        machine.current = m_definition->machine(node.graphNode).entryState;
        machine.stateTime = 0.0f;
        if (machine.stateCount > 0) {
            stateNode(machine, machine.current).fade.fadeIn = 1.0f;
        }
    }
}

void AnimGraphInstance::setTrigger(const std::string& name) {
    writeTrigger(name, 1);
}

void AnimGraphInstance::resetTrigger(const std::string& name) {
    writeTrigger(name, 0);
}

void AnimGraphInstance::update(float deltaTime) {
    for (uint32_t i = 0; i < m_nodes.size();) {
        auto& node = m_nodes[i];
        if (i != 0) {
            const auto& parent = m_nodes[node.parent];
            node.fade.step(deltaTime);
            node.weight = parent.weight * node.fade.blend();
            node.active = parent.active && m_machines[parent.data].current == node.state;
        }
        if (node.weight <= 0.0f) {
            i += node.subtreeSize;
            continue;
        }
        if (node.machine) {
            updateMachine(node, deltaTime);
        } else {
            auto& clip = m_clips[node.data];
            clip.time += deltaTime * clip.rate;
            if (clip.duration > 0.0f) {
                if (clip.looping) {
                    clip.time = std::fmod(clip.time, clip.duration);
                    if (clip.time < 0.0f) clip.time += clip.duration;
                } else {
                    clip.time = std::clamp(clip.time, 0.0f, clip.duration);
                }
            }
        }
        ++i;
    }
}

void AnimGraphInstance::updateMachine(const RuntimeNode& node, float deltaTime) {
    auto& machine = m_machines[node.data];
    if (machine.stateCount == 0) {
        return;
    }

    machine.stateTime += deltaTime;
    if (!node.active) {
        return;
    }
    const auto* transition = findTransition(m_definition->machine(node.graphNode), machine);
    if (!transition) {
        return;
    }

    machine.current = transition->to;
    machine.stateTime = 0.0f;
    const uint32_t entered = m_stateNodes[machine.firstStateNode + machine.current];
    if (m_nodes[entered].fade.blend() <= 0.0f) {
        reset(entered);
    }
    for (uint32_t state = 0; state < machine.stateCount; ++state) {
        auto& fade = stateNode(machine, state).fade;
        if (state != machine.current && fade.blend() > 0.0f) {
            fade.fadeOutWithin(transition->fadeDuration);
        }
    }
    m_nodes[entered].fade.startFadeIn(transition->fadeDuration);
}

const AnimTransition* AnimGraphInstance::findTransition(const StateMachineNode& machine,
                                                        const MachineRuntime& runtime) {
    for (const auto& transition : AnimGraph::transitions(machine, runtime.current)) {
        if (runtime.stateTime < transition.exitTime) {
            continue;
        }
        const uint32_t trigger = transition.trigger;
        if (trigger != kNone && !m_triggers[trigger]) {
            continue;
        }
        if (trigger != kNone) {
            m_triggers[trigger] = 0;
        }
        return &transition;
    }
    return nullptr;
}

bool AnimGraphInstance::evaluate(const Skeleton& skeleton, std::span<const AssetPtr<AnimationClip>> clips,
                                 std::vector<NodeTransform>& out) {
    if (m_machines[0].stateCount == 0) {
        return false;
    }
    evaluateNode(0, skeleton, clips, out);
    return true;
}

void AnimGraphInstance::evaluateNode(uint32_t index, const Skeleton& skeleton,
                                     std::span<const AssetPtr<AnimationClip>> clips,
                                     std::vector<NodeTransform>& out) {
    const auto& node = m_nodes[index];
    if (!node.machine) {
        const auto& clip = m_clips[node.data];
        if (clip.clip < clips.size() && clips[clip.clip]) {
            samplePose(clip.time, skeleton, *clips[clip.clip], out);
        } else {
            out = skeleton.restPose;
        }
        return;
    }

    const auto& machine = m_machines[node.data];
    float accumulated = 0.0f;
    for (uint32_t state = 0; state < machine.stateCount; ++state) {
        const uint32_t child = m_stateNodes[machine.firstStateNode + state];
        const float weight = m_nodes[child].fade.blend();
        if (weight <= 0.0f) {
            continue;
        }
        if (accumulated == 0.0f) {
            evaluateNode(child, skeleton, clips, out);
        } else {
            auto& scratch = m_scratch[node.depth + 1];
            evaluateNode(child, skeleton, clips, scratch);
            blendPose(out, scratch, weight / (accumulated + weight));
        }
        accumulated += weight;
    }
    if (accumulated == 0.0f) {
        out = skeleton.restPose;
    }
}

uint32_t AnimGraphInstance::currentState(uint32_t machineNode) const {
    return machineAt(machineNode).current;
}

float AnimGraphInstance::stateTime(uint32_t machineNode) const {
    return machineAt(machineNode).stateTime;
}

float AnimGraphInstance::weight(uint32_t node) const {
    assert(m_runtimeIndex[node] != kNone);
    float weight = 1.0f;
    for (uint32_t i = m_runtimeIndex[node]; i != kNone; i = m_nodes[i].parent) {
        weight *= m_nodes[i].fade.blend();
    }
    return weight;
}

float AnimGraphInstance::blend(uint32_t node) const {
    assert(m_runtimeIndex[node] != kNone);
    return m_nodes[m_runtimeIndex[node]].fade.blend();
}

float AnimGraphInstance::clipTime(uint32_t clipNode) const {
    const auto& node = m_nodes[m_runtimeIndex[clipNode]];
    assert(!node.machine);
    return m_clips[node.data].time;
}

const AnimGraphInstance::MachineRuntime& AnimGraphInstance::machineAt(uint32_t graphNode) const {
    const auto& node = m_nodes[m_runtimeIndex[graphNode]];
    assert(node.machine);
    return m_machines[node.data];
}

void AnimGraphInstance::writeTrigger(const std::string& name, uint8_t value) {
    auto trigger = m_definition->findTrigger(name);
    assert(trigger.has_value());
    if (trigger) {
        m_triggers[*trigger] = value;
    }
}

} // namespace ailo
