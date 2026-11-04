#include "AnimationStateMachine.h"

#include <cassert>

namespace ailo {

uint32_t AnimationStateMachine::addState(uint32_t clip, bool looping) {
    auto index = static_cast<uint32_t>(m_states.size());
    m_states.push_back({
        .clip = clip,
        .looping = looping,
        .firstTransition = static_cast<uint32_t>(m_transitions.size()),
    });
    return index;
}

uint32_t AnimationStateMachine::addTrigger(const std::string& name) {
    return m_triggerNames.try_emplace(name, static_cast<uint32_t>(m_triggerNames.size())).first->second;
}

void AnimationStateMachine::addTransition(uint32_t from, uint32_t to, const std::string& trigger, float fadeDuration) {
    assert(from < m_states.size() && to < m_states.size());

    auto& state = m_states[from];
    auto position = m_transitions.begin() + state.firstTransition + state.transitionCount;
    m_transitions.insert(position, { .to = to, .trigger = addTrigger(trigger), .fadeDuration = fadeDuration });
    state.transitionCount++;

    for (uint32_t i = from + 1; i < m_states.size(); ++i) {
        m_states[i].firstTransition++;
    }
}

std::span<const AnimationStateMachine::Transition> AnimationStateMachine::transitions(uint32_t state) const {
    const auto& s = m_states[state];
    return std::span(m_transitions).subspan(s.firstTransition, s.transitionCount);
}

std::optional<uint32_t> AnimationStateMachine::findTrigger(const std::string& name) const {
    auto it = m_triggerNames.find(name);
    if (it == m_triggerNames.end()) {
        return std::nullopt;
    }
    return it->second;
}

AnimationStateMachineInstance::AnimationStateMachineInstance(const AnimationStateMachine& definition)
    : m_definition(&definition), m_triggers(definition.triggerCount(), 0) {}

void AnimationStateMachineInstance::setTrigger(const std::string& name) {
    writeTrigger(name, 1);
}

void AnimationStateMachineInstance::resetTrigger(const std::string& name) {
    writeTrigger(name, 0);
}

const AnimationStateMachine::Transition* AnimationStateMachineInstance::update() {
    if (m_definition->stateCount() == 0) {
        return nullptr;
    }

    for (const auto& transition : m_definition->transitions(m_currentState)) {
        if (m_triggers[transition.trigger]) {
            m_triggers[transition.trigger] = 0;
            m_currentState = transition.to;
            return &transition;
        }
    }
    return nullptr;
}

void AnimationStateMachineInstance::writeTrigger(const std::string& name, uint8_t value) {
    auto trigger = m_definition->findTrigger(name);
    assert(trigger.has_value());
    if (trigger) {
        m_triggers[*trigger] = value;
    }
}

} // namespace ailo
