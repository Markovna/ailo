#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/AssetServer.h"
#include "render/Animation.h"

namespace ailo {

class AnimationStateMachine {
public:
    static constexpr uint32_t kNoTrigger = std::numeric_limits<uint32_t>::max();

    struct State {
        std::string clip;        // AnimationClip::name
        bool     looping         = true;
        uint32_t firstTransition = 0;
        uint32_t transitionCount = 0;
    };

    struct Transition {
        uint32_t to           = 0;
        uint32_t trigger      = kNoTrigger;
        // Seconds the from-state must have been active before the transition can fire.
        float    exitTime     = 0.0f;
        float    fadeDuration = 0.0f;
    };

    uint32_t addState(std::string clip, bool looping = true);
    uint32_t addTrigger(const std::string& name);
    void addTransition(uint32_t from, uint32_t to, const std::string& trigger = {}, float fadeDuration = 0.0f,
                       float exitTime = 0.0f);

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

class AnimationStateMachineInstance {
public:
    // Resolves each state's clip name to an index into clips. Throws std::invalid_argument if a clip is missing.
    AnimationStateMachineInstance(AssetPtr<AnimationStateMachine> definition,
                                  std::span<const AssetPtr<AnimationClip>> clips);

    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    const AnimationStateMachine::Transition* update(float deltaTime);

    uint32_t currentStateIndex() const { return m_currentState; }
    float stateTime() const { return m_stateTime; }
    const AnimationStateMachine::State& currentState() const { return m_definition->state(m_currentState); }
    uint32_t currentClip() const { return m_stateClips[m_currentState]; }
    const AnimationStateMachine& definition() const { return *m_definition; }

private:
    void writeTrigger(const std::string& name, uint8_t value);

    AssetPtr<AnimationStateMachine> m_definition;
    std::vector<uint32_t> m_stateClips;
    std::vector<uint8_t> m_triggers;
    uint32_t m_currentState = 0;
    float m_stateTime = 0.0f;
};

} // namespace ailo
