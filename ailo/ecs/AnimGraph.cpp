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

AnimGraph::Builder& AnimGraph::Builder::transition(uint16_t from, uint16_t to, std::string trigger,
                                                   float fadeDuration, float exitTime) {
    m_transitions.push_back({
        .from = from,
        .to = to,
        .trigger = std::move(trigger),
        .fadeDuration = fadeDuration,
        .exitTime = exitTime,
    });
    return *this;
}

AnimGraph::Builder& AnimGraph::Builder::entryState(uint16_t state) {
    m_entryState = state;
    return *this;
}

AnimGraph AnimGraph::Builder::build() const {
    AnimGraph graph;
    build(graph, graph.addNode(StateMachineNode {}));
    return graph;
}

// Each machine's node is added before its states are, so the nodes end up in preorder.
void AnimGraph::Builder::build(AnimGraph& graph, NodeIndex self) const {
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
            m_machines[desc.machine].build(graph, child);
            machine.states[i].node = child;
        }
    }

    std::vector<const TransitionDesc*> sorted;
    sorted.reserve(m_transitions.size());
    for (const auto& transition : m_transitions) {
        if (transition.from >= m_states.size() || transition.to >= m_states.size()) {
            throw std::invalid_argument(std::format("animation graph: transition {} -> {} of machine {} is out of range",
                                                    transition.from, transition.to, self.value));
        }
        sorted.push_back(&transition);
    }
    std::ranges::stable_sort(sorted, {}, &TransitionDesc::from);

    machine.transitions.reserve(sorted.size());
    for (const TransitionDesc* transition : sorted) {
        auto& from = machine.states[transition->from];
        if (from.transitionCount == 0) {
            from.firstTransition = static_cast<uint16_t>(machine.transitions.size());
        }
        from.transitionCount++;
        machine.transitions.push_back({
            .to = transition->to,
            .trigger = graph.addTrigger(transition->trigger),
            .exitTime = transition->exitTime,
            .fadeDuration = transition->fadeDuration,
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

template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

AnimGraphRunner::AnimGraphRunner(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips)
    : m_definition(std::move(definition)) {
    assert(m_definition);
    const AnimGraph& graph = *m_definition;
    m_triggers.assign(graph.triggerCount(), 0);
    m_nodesMap.assign(graph.nodeCount(), kNoTreeNode);
    m_tree.reserve(graph.nodeCount());

    // Depth-first, so m_tree is in preorder: every node comes after its parent.
    auto build = [&](this auto& self, NodeIndex graphNode) -> void {
        if (graphNode.value >= graph.nodeCount()) {
            throw std::invalid_argument(
                std::format("animation graph: state refers to missing node {}", graphNode.value));
        }
        if (m_nodesMap[graphNode.value] != kNoTreeNode) {
            throw std::invalid_argument(
                std::format("animation graph: node {} is reached more than once", graphNode.value));
        }
        const auto index = static_cast<uint16_t>(m_tree.size());
        m_nodesMap[graphNode.value] = index;

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
                if (machine.states.empty()) {
                    throw std::invalid_argument(std::format("animation graph: machine {} has no states", graphNode.value));
                }
                m_tree.push_back({
                    .data = StateMachine { .machine = graphNode }
                });
                for (const auto& state : machine.states) {
                    assert(depth < std::numeric_limits<uint8_t>::max());
                    self(state.node);
                }

                auto& sm = std::get<StateMachine>(m_tree[index].data);
                sm.currentState = machine.entryState;
                sm.currentNode = m_nodesMap[machine.states[machine.entryState].node.value];
            },
        }, graph.node(graphNode));
    };
    build(AnimGraph::kRoot);
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

void AnimGraphRunner::update(float dt) {
    for (auto& node : m_tree) {
        node.wasActive = std::exchange(node.active, false);
    }

    m_tree[0].active = true;

    for (auto& node : m_tree) {
        if (!node.active) {
            continue;
        }

        if (!node.wasActive) {
            resetNode(node);
        }

        std::visit(Overloaded {
            [&](Clip& clip) { updateClip(clip, dt); },
            [&](StateMachine& sm) { updateMachine(sm, dt); },
        }, node.data);
    }
}

uint16_t AnimGraphRunner::currentState(NodeIndex machine) const {
    return std::get<StateMachine>(m_tree[m_nodesMap[machine.value]].data).currentState;
}

void AnimGraphRunner::resetNode(Node& node) {
    std::visit(Overloaded {
        [&](Clip& clip) {
            clip.time = clip.rate < 0.0f ? clip.duration : 0.0f;
        },
        [&](StateMachine& sm) {
            const auto& machine = std::get<StateMachineNode>(m_definition->node(sm.machine));
            sm.currentState = machine.entryState;
            sm.currentNode = m_nodesMap[machine.states[sm.currentState].node.value];
            sm.stateTime = 0.0f;
            sm.m_transitionStack.clear();
        },
    }, node.data);
}

void AnimGraphRunner::evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out) {
    evaluateNode(0, skeleton, posePool, out);
}

void AnimGraphRunner::evaluateNode(uint16_t index, const Skeleton& skeleton, PosePool& posePool,
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
    }, m_tree[index].data);
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

void AnimGraphRunner::updateMachine(StateMachine& sm, float dt) {
    sm.stateTime += dt;

    auto& machine = std::get<StateMachineNode>(m_definition->node(sm.machine));
    auto transitions = machine.transitionsFrom(sm.currentState);

    for (auto& transition : transitions) {
        if (sm.stateTime < transition.exitTime) continue;

        const uint16_t trigger = transition.trigger;
        if (trigger != kNone && !m_triggers[trigger]) continue;
        if (trigger != kNone) {
            m_triggers[trigger] = 0;
        }

        auto& states = machine.states;
        const uint16_t to = m_nodesMap[states[transition.to].node.value];

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
        m_tree[sm.currentNode].active = true;
        return;
    }

    m_tree[stack.back().fromNode].active = true;
    for (const auto& transition : stack) {
        m_tree[transition.toNode].active = true;
    }
}

} // namespace ailo
