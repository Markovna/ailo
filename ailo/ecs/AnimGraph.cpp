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

template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

AnimGraphRunner::AnimGraphRunner(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips)
    : m_definition(std::move(definition)) {
    assert(m_definition);
    const AnimGraph& graph = *m_definition;
    m_triggers.assign(graph.triggerCount(), 0);
    m_nodesMap.assign(graph.nodeCount(), kNone);
    m_tree.reserve(graph.nodeCount());

    // Depth-first, so m_tree is in preorder: every node comes after its parent.
    auto build = [&](this auto& self, uint32_t graphNode, uint8_t depth) -> void {
        if (graphNode >= graph.nodeCount()) {
            throw std::invalid_argument(std::format("animation graph: state refers to missing node {}", graphNode));
        }
        if (m_nodesMap[graphNode] != kNone) {
            throw std::invalid_argument(std::format("animation graph: node {} is reached more than once", graphNode));
        }
        const auto index = static_cast<uint32_t>(m_tree.size());
        m_nodesMap[graphNode] = index;

        if (const auto* clipNode = std::get_if<ClipNode>(&graph.node(graphNode))) {
            auto it = std::ranges::find_if(clips, [&](const auto& clip) { return clip && clip->name == clipNode->clip; });
            if (it == clips.end()) {
                throw std::invalid_argument(std::format("animation graph: no clip named '{}'", clipNode->clip));
            }
            Clip clip {
                .clip = *it,
                .rate = clipNode->rate,
                .looping = clipNode->looping,
                .duration = (*it)->duration,
            };
            clip.time = clip.rate < 0.0f ? clip.duration : 0.0f;
            m_tree.push_back({ .data = std::move(clip), .depth = depth });
            return;
        }

        const auto& machine = graph.machine(graphNode);
        if (machine.states.empty()) {
            throw std::invalid_argument(std::format("animation graph: machine {} has no states", graphNode));
        }
        m_tree.push_back({
            .data = StateMachine { .sm = &machine, .currentState = machine.entryState },
            .depth = depth,
        });
        for (const auto& state : machine.states) {
            assert(depth < std::numeric_limits<uint8_t>::max());
            self(state.node, depth + 1);
        }

        auto& sm = std::get<StateMachine>(m_tree[index].data);
        sm.currentNode = m_nodesMap[machine.states[machine.entryState].node];
    };
    build(0, 0);
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

    for (uint32_t i = 0; i < m_tree.size(); ++i) {
        auto& node = m_tree[i];
        if (!node.active) {
            if (node.wasActive) {
                std::visit(Overloaded {
                    [&](Clip& clip) {
                        clip.time = clip.rate < 0.0f ? clip.duration : 0.0f;
                    },
                    [&](StateMachine& sm) {
                        sm.currentState = sm.sm->entryState;
                        sm.currentNode = m_nodesMap[sm.sm->states[sm.currentState].node];
                        sm.stateTime = 0.0f;
                        sm.m_transitionStack.clear();
                    },
                }, node.data);
            }
            continue;
        }

        std::visit(Overloaded {
            [&](Clip& clip) { updateClip(clip, dt); },
            [&](StateMachine& sm) { updateMachine(sm, dt); },
        }, node.data);
    }
}

void AnimGraphRunner::evaluate(Skeleton& skeleton, PosePool& posePool, std::vector<NodeTransform>& out) {
    evaluateNode(0, skeleton, posePool, out);
}

void AnimGraphRunner::evaluateNode(uint32_t index, const Skeleton& skeleton, PosePool& posePool,
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

    auto& machine = *sm.sm;
    auto& states = machine.states;
    auto transitions = m_definition->transitions(machine, sm.currentState);

    for (auto& transition : transitions) {
        if (sm.stateTime < transition.exitTime) continue;

        const uint32_t trigger = transition.trigger;
        if (trigger != kNone && !m_triggers[trigger]) continue;
        if (trigger != kNone) {
            m_triggers[trigger] = 0;
        }

        const uint32_t to   = m_nodesMap[states[transition.to].node];
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

    for (const auto& transition : stack) {
        m_tree[transition.toNode].active = true;
    }
    m_tree[stack.back().fromNode].active = true;
}

} // namespace ailo
