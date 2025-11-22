#include "InputSystem.h"
#include <iostream>
#include <utility>

namespace ailo {

// Helper structure to access InputSystem from window user pointer
// This must match the structure defined in the main application
struct WindowUserData {
    void* app;
    InputSystem* inputSystem;
};

void InputSystem::init() {
    // Initialize mouse position
    m_mouseX = 0;
    m_mouseY = 0;

    m_lastMouseX = m_mouseX;
    m_lastMouseY = m_mouseY;

    // Initialize key and button states
    m_keyStates.fill(false);
    m_mouseButtonStates.fill(false);
}

void InputSystem::shutdown() {
    clearEvents();
}

void InputSystem::processEvents() {
    // Reset mouse delta each frame
    m_mouseDeltaX = 0.0;
    m_mouseDeltaY = 0.0;
}

bool InputSystem::pollEvent(Event& outEvent) {
    if (m_eventQueue.empty()) {
        return false;
    }

    outEvent = m_eventQueue.front();
    m_eventQueue.pop();
    updateState(outEvent);
    return true;
}

void InputSystem::updateState(Event& event) {
    std::visit([this](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ailo::KeyPressedEvent>) {
            auto keyIndex = static_cast<uint16_t>(e.keyCode);
            if (keyIndex < m_keyStates.size()) {
                m_keyStates[keyIndex] = true;
            }
        }

        if constexpr (std::is_same_v<T, ailo::KeyReleasedEvent>) {
            auto keyIndex = static_cast<uint16_t>(e.keyCode);
            if (keyIndex < m_keyStates.size()) {
                m_keyStates[keyIndex] = false;
            }
        }

        if constexpr (std::is_same_v<T, ailo::MouseButtonPressedEvent>) {
            uint8_t buttonIndex = static_cast<uint8_t>(e.button);
            if (buttonIndex < m_mouseButtonStates.size()) {
              m_mouseButtonStates[buttonIndex] = true;
            }
        }

        if constexpr (std::is_same_v<T, ailo::MouseButtonReleasedEvent>) {
            uint8_t buttonIndex = static_cast<uint8_t>(e.button);
            if (buttonIndex < m_mouseButtonStates.size()) {
              m_mouseButtonStates[buttonIndex] = false;
            }
        }

        if constexpr (std::is_same_v<T, ailo::MouseMovedEvent>) {
          // Update mouse position and calculate delta
          m_lastMouseX = m_mouseX;
          m_lastMouseY = m_mouseY;
          m_mouseX = e.x;
          m_mouseY = e.y;
          m_mouseDeltaX = e.x - m_lastMouseX;
          m_mouseDeltaY = e.y - m_lastMouseY;

          e.deltaX = m_mouseDeltaX;
          e.deltaY = m_mouseDeltaY;
        }

    }, event);
}

bool InputSystem::isKeyPressed(KeyCode key) const {
    uint16_t keyIndex = static_cast<uint16_t>(key);
    if (keyIndex >= m_keyStates.size()) {
        return false;
    }
    return m_keyStates[keyIndex];
}

bool InputSystem::isMouseButtonPressed(MouseButton button) const {
    uint8_t buttonIndex = static_cast<uint8_t>(button);
    if (buttonIndex >= m_mouseButtonStates.size()) {
        return false;
    }
    return m_mouseButtonStates[buttonIndex];
}

void InputSystem::getMousePosition(double& x, double& y) const {
    x = m_mouseX;
    y = m_mouseY;
}

void InputSystem::getMouseDelta(double& deltaX, double& deltaY) const {
    deltaX = m_mouseDeltaX;
    deltaY = m_mouseDeltaY;
}

void InputSystem::pushEvent(Event&& event) {
    m_eventQueue.push(std::move(event));
}

} // namespace ailo
