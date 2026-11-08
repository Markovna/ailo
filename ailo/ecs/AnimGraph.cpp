#include "AnimGraph.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <stdexcept>

namespace ailo {

namespace {

constexpr uint32_t kNone = AnimGraph::kNone;

template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

}

AnimGraph::AnimGraph() {
    m_nodes.emplace_back(StateMachineNode {});
}

uint32_t AnimGraph::addState(std::string clip, bool looping) {
    const auto node = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back(ClipNode { .clip = std::move(clip), .looping = looping });

    auto& machine = std::get<StateMachineNode>(m_nodes[0]);
    const auto index = static_cast<uint32_t>(machine.states.size());
    machine.states.push_back({
        .node = node,
        .firstTransition = static_cast<uint32_t>(machine.transitions.size()),
    });
    return index;
}

uint32_t AnimGraph::addTrigger(const std::string& name) {
    if (name.empty()) return kNone;
    return m_triggerNames.try_emplace(name, static_cast<uint32_t>(m_triggerNames.size())).first->second;
}

void AnimGraph::addTransition(uint32_t from, uint32_t to, const std::string& trigger, float fadeDuration,
                              float exitTime) {
    auto& machine = std::get<StateMachineNode>(m_nodes[0]);
    auto& states = machine.states;
    assert(from < states.size() && to < states.size());

    auto& state = states[from];
    auto position = machine.transitions.begin() + state.firstTransition + state.transitionCount;
    machine.transitions.insert(position, {
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


AnimGraphInstance::AnimGraphInstance(AssetPtr<AnimGraph> definition, std::span<const AssetPtr<AnimationClip>> clips)
    : m_definition(std::move(definition)) {
    assert(m_definition);
    const auto& graph = *m_definition;
    m_triggers.resize(graph.triggerCount(), 0);

    m_clipDurations.reserve(clips.size());
    for (const auto& clip : clips) {
        m_clipDurations.push_back(clip ? clip->duration : 0.0f);
    }

    m_nodeClips.assign(graph.nodeCount(), kNone);
    for (uint32_t i = 0; i < graph.nodeCount(); ++i) {
        const auto* node = std::get_if<ClipNode>(&graph.node(i));
        if (!node) continue;
        auto it = std::ranges::find_if(clips, [&](const auto& clip) { return clip && clip->name == node->clip; });
        if (it == clips.end()) {
            throw std::invalid_argument(std::format("animation graph: no clip named '{}'", node->clip));
        }
        m_nodeClips[i] = static_cast<uint32_t>(it - clips.begin());
    }

    for (const auto& state : graph.root().states) {
        if (std::holds_alternative<StateMachineNode>(graph.node(state.node))) {
            throw std::invalid_argument("animation graph: nested state machines aren't supported yet");
        }
    }
    m_current = graph.root().entryState;
}

void AnimGraphInstance::setTrigger(const std::string& name) {
    writeTrigger(name, 1);
}

void AnimGraphInstance::resetTrigger(const std::string& name) {
    writeTrigger(name, 0);
}

const AnimGraph::Transition* AnimGraphInstance::update(float deltaTime) {
    if (m_definition->stateCount() == 0) {
        return nullptr;
    }
    if (!m_started) {
        m_started = true;
        enterState(m_current, 0.0f);
    }

    m_stateTime += deltaTime;
    const auto* transition = findTransition();
    if (transition) {
        m_current = transition->to;
        m_stateTime = 0.0f;
        // A transition always plays the target afresh, even when it is still fading out, so re-entering restarts it.
        enterState(m_current, transition->fadeDuration);
    }
    return transition;
}

uint32_t AnimGraphInstance::currentClip() const {
    if (m_definition->stateCount() == 0) return kNone;
    return m_nodeClips[m_definition->state(m_current).node];
}

std::vector<NodeTransform>* AnimGraphInstance::computePose(const Skeleton& skeleton, const std::vector<AssetPtr<AnimationClip>>& clips) {
    // TODO
    return nullptr;
}

void AnimGraphInstance::writeTrigger(const std::string& name, uint8_t value) {
    auto trigger = m_definition->findTrigger(name);
    assert(trigger.has_value());
    if (trigger) {
        m_triggers[*trigger] = value;
    }
}

const AnimGraph::Transition* AnimGraphInstance::findTransition() {
    for (const auto& transition : m_definition->transitions(m_current)) {
        if (m_stateTime < transition.exitTime) {
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

void AnimGraphInstance::enterState(uint32_t state, float fadeDuration) {
    const uint32_t node = m_definition->state(state).node;
    std::visit(Overloaded {
        [&](const ClipNode& clip) {
            const uint32_t index = m_nodeClips[node];

        },
        [&](const StateMachineNode&) {

        },
    }, m_definition->node(node));
}

} // namespace ailo
