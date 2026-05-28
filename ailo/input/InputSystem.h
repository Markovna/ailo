#pragma once

#include "InputTypes.h"
#include <queue>
#include <array>
#include <functional>
#include <vector>

#include "entt/container/dense_map.hpp"
#include "entt/core/fwd.hpp"
#include "entt/core/type_info.hpp"

namespace ailo {

class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() = default;

    // Process all queued events
    void processEvents();

    // Check if there are any events in the queue
    bool hasEvents() const { return !m_eventQueue.empty(); }

    // Get the next event from the queue
    bool pollEvent(Event& outEvent);

    void pushEvent(Event&& event);

    // Query current input state
    bool isKeyPressed(KeyCode key) const;
    bool isMouseButtonPressed(MouseButton button) const;
    void getMousePosition(double& x, double& y) const;
    void getMouseDelta(double& deltaX, double& deltaY) const;

    // Clear the event queue
    void clearEvents() { m_eventQueue = std::queue<Event>(); }

    template<typename T>
    void subscribe(std::function<void(const T&)> callback) {
        auto id = entt::type_hash<T>::value();
        m_subscribers[id].emplace_back([cb = std::move(callback)](const void* e) {
            cb(*static_cast<const T*>(e));
        });
    }

private:
    std::queue<Event> m_eventQueue;
    entt::dense_map<entt::id_type, std::vector<std::function<void(const void*)>>> m_subscribers;

    // Current input state
    std::array<bool, 512> m_keyStates{}; // Track key states
    std::array<bool, 8> m_mouseButtonStates{}; // Track mouse button states
    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    double m_mouseDeltaX = 0.0;
    double m_mouseDeltaY = 0.0;

    template<typename T>
    void notify(const T& e) {
        auto it = m_subscribers.find(entt::type_hash<T>::value());
        if (it != m_subscribers.end()) {
            for (auto& cb : it->second) {
                cb(&e);
            }
        }
    }

    void onEvent(Event&);
    void onKeyPressed(KeyPressedEvent&);
    void onKeyReleased(KeyReleasedEvent&);
    void onMouseButtonPressed(MouseButtonPressedEvent&);
    void onMouseButtonReleased(MouseButtonReleasedEvent&);
    void onMouseMoved(MouseMovedEvent&);
};

} // namespace ailo
