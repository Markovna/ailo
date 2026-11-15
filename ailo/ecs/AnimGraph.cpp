#include "AnimGraph.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

#include "PoseOps.h"

namespace ailo {

namespace {

constexpr uint16_t kNone = AnimGraph::kNone;

}

AnimGraph::Builder& AnimGraph::Builder::state(std::string clip, bool looping, float rate) {
    m_states.push_back({ .clip = { .clip = std::move(clip), .rate = rate, .looping = looping } });
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::state(Builder machine) {
    m_states.push_back({ .machine = static_cast<uint16_t>(m_machines.size()) });
    m_machines.push_back(std::move(machine));
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::transition(uint16_t from, uint16_t to, TransitionDesc desc) {
    m_transitions.push_back({ .from = from, .to = to, .desc = std::move(desc) });
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::parameter(std::string name, float defaultValue) {
    m_parameters.push_back({ .name = std::move(name), .defaultValue = defaultValue });
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::entryState(uint16_t state) {
    m_entryState = state;
    return *this;
}

AnimGraph AnimGraph::Builder::build() const {
    AnimGraph graph;
    // Collected up front: a condition may read a parameter declared by a machine that is built after it.
    collectParameters(graph);

    std::vector<std::string_view> parameters;
    parameters.reserve(graph.m_parameters.size());
    for (const auto& parameter : graph.m_parameters) {
        parameters.push_back(parameter.name);
    }

    build(graph, graph.addNode(StateMachineNode {}), parameters);
    return graph;
}

void AnimGraph::Builder::collectParameters(AnimGraph& graph) const {
    for (const auto& parameter : m_parameters) {
        graph.addParameter(parameter.name, parameter.defaultValue);
    }
    for (const auto& machine : m_machines) {
        machine.collectParameters(graph);
    }
}

// Each machine's node is added before its states are, and a nested machine's subtree is finished before the next
// state is added, so the nodes end up in the preorder AnimGraph promises (AnimGraphRunner relies on it).
void AnimGraph::Builder::build(AnimGraph& graph, NodeIndex self, std::span<const std::string_view> parameters) const {
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
    if (auto it = m_parameterIndices.find(name); it != m_parameterIndices.end()) {
        const float existing = m_parameters[it->second].defaultValue;
        if (existing != defaultValue) {
            throw std::invalid_argument(std::format(
                "animation graph: parameter '{}' is declared with defaults {} and {}", name, existing, defaultValue));
        }
        return;
    }
    if (m_parameters.size() > expr::kMaxData) {
        throw std::invalid_argument("animation graph: too many parameters");
    }
    m_parameterIndices.emplace(name, static_cast<uint16_t>(m_parameters.size()));
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
        std::visit(Overloaded {
            [&](const ClipNode& clipNode) {
                auto it = std::ranges::find_if(clips, [&](const auto& clip) { return clip && clip->name == clipNode.clip; });
                if (it == clips.end()) {
                    throw std::invalid_argument(std::format("animation graph: no clip named '{}'", clipNode.clip));
                }
                Clip clip {
                    .clip = *it,
                    .rate = clipNode.rate,
                    .looping = clipNode.looping,
                    .duration = (*it)->duration,
                };
                clip.time = clip.rate < 0.0f ? clip.duration : 0.0f;
                m_tree.push_back({ .data = std::move(clip) });
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
        node.wasActive = std::exchange(node.active, false);
    }

    m_tree[AnimGraph::kRoot.value].active = true;

    // Preorder: a machine marks its active children before the loop reaches them.
    for (uint16_t i = 0; i < m_tree.size(); ++i) {
        auto& node = m_tree[i];
        if (!node.active) {
            continue;
        }

        if (!node.wasActive) {
            resetNode(NodeIndex { i });
        }

        std::visit(Overloaded {
            [&](Clip& clip) { updateClip(clip, dt); },
            [&](StateMachine&) { updateMachine(NodeIndex { i }, dt); },
        }, node.data);
    }
}

uint16_t AnimGraphRunner::currentState(NodeIndex machine) const {
    return std::get<StateMachine>(m_tree[machine.value].data).currentState;
}

void AnimGraphRunner::resetNode(NodeIndex index) {
    std::visit(Overloaded {
        [&](Clip& clip) {
            clip.time = clip.rate < 0.0f ? clip.duration : 0.0f;
        },
        [&](StateMachine& sm) {
            const auto& machine = std::get<StateMachineNode>(m_definition->node(index));
            sm.currentState = machine.entryState;
            sm.currentNode = machine.states[sm.currentState].node;
            sm.stateTime = 0.0f;
            sm.m_transitionStack.clear();
        },
    }, m_tree[index.value].data);
}

void AnimGraphRunner::evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out) {
    evaluateNode(AnimGraph::kRoot, skeleton, posePool, out);
}

void AnimGraphRunner::evaluateNode(NodeIndex index, const Skeleton& skeleton, PosePool& posePool,
                                   std::vector<NodeTransform>& out) {
    std::visit(Overloaded {
        [&](const Clip& clip) {
            if (const AnimationClip* asset = clip.clip.get()) {
                samplePose(clip.time, skeleton, *asset, out);
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
    }, m_tree[index.value].data);
}

void AnimGraphRunner::updateClip(Clip& clip, float dt) {
    clip.time += dt * clip.rate;
    if (clip.duration <= 0.0f) return;
    if (clip.looping) {
        clip.time = std::fmod(clip.time, clip.duration);
        if (clip.time < 0.0f) clip.time += clip.duration;
    } else {
        clip.time = std::clamp(clip.time, 0.0f, clip.duration);
    }
}

void AnimGraphRunner::updateMachine(NodeIndex index, float dt) {
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

        // newest first
        sm.m_transitionStack.insert(sm.m_transitionStack.begin(), Transition {
            .fromNode = sm.currentNode,
            .toNode = to,
            .duration = transition.fadeDuration
        });
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

    if (stack.empty()) {
        m_tree[sm.currentNode.value].active = true;
        return;
    }

    m_tree[stack.back().fromNode.value].active = true;
    for (const auto& transition : stack) {
        m_tree[transition.toNode.value].active = true;
    }
}

} // namespace ailo
