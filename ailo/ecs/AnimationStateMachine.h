#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ailo {

class AnimationStateMachine {
public:
    struct State {
        uint32_t clip            = 0;
        bool     looping         = true;
        uint32_t firstTransition = 0;
        uint32_t transitionCount = 0;
    };

    struct Transition {
        uint32_t to           = 0;
        uint32_t trigger      = 0;
        float    fadeDuration = 0.0f;
    };

    uint32_t addState(uint32_t clip, bool looping = true);
    uint32_t addTrigger(const std::string& name);
    void addTransition(uint32_t from, uint32_t to, const std::string& trigger, float fadeDuration = 0.0f);

    uint32_t stateCount() const { return static_cast<uint32_t>(m_states.size()); }
    uint32_t triggerCount() const { return static_cast<uint32_t>(m_triggerNames.size()); }
    const State& state(uint32_t index) const { return m_states[index]; }
    std::span<const Transition> transitions(uint32_t state) const;
    std::optional<uint32_t> findTrigger(const std::string& name) const;

private:
    std::vector<State> m_states;
    std::vector<Transition> m_transitions;
    std::unordered_map<std::string, uint32_t> m_triggerNames;
};

// Does not own the definition, which must outlive the instance and not change once the instance exists.
class AnimationStateMachineInstance {
public:
    explicit AnimationStateMachineInstance(const AnimationStateMachine& definition);

    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    const AnimationStateMachine::Transition* update();

    uint32_t currentStateIndex() const { return m_currentState; }
    const AnimationStateMachine::State& currentState() const { return m_definition->state(m_currentState); }

private:
    void writeTrigger(const std::string& name, uint8_t value);

    const AnimationStateMachine* m_definition;
    std::vector<uint8_t> m_triggers;
    uint32_t m_currentState = 0;
};

} // namespace ailo
