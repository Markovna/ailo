#pragma once

#include "InputTypes.h"
#include <GLFW/glfw3.h>
#include <queue>
#include <array>
#include <functional>

namespace ailo {

class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() = default;

    // Initialize the input system with a GLFW window
    void init();

    // Shutdown and cleanup
    void shutdown();

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

private:
    std::queue<Event> m_eventQueue;

    // Current input state
    std::array<bool, 512> m_keyStates{}; // Track key states
    std::array<bool, 8> m_mouseButtonStates{}; // Track mouse button states
    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    double m_mouseDeltaX = 0.0;
    double m_mouseDeltaY = 0.0;

    void updateState(Event&);
};

} // namespace ailo
