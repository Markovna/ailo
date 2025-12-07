#include "InputSystem.h"
#include <utils/Utils.h>
#include <utility>

namespace ailo {

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
    onEvent(outEvent);
    return true;
}

void InputSystem::onEvent(Event& event) {
    std::visit(utils::overloaded {
      [this](KeyPressedEvent& e) { onKeyPressed(e); },
      [this](KeyReleasedEvent& e) { onKeyReleased(e); },
      [this](MouseButtonPressedEvent& e) { onMouseButtonPressed(e); },
      [this](MouseButtonReleasedEvent& e) { onMouseButtonReleased(e); },
      [this](MouseMovedEvent& e) { onMouseMoved(e); },
      [](auto&& e) {  }
    }, event);
}

void InputSystem::onKeyPressed(KeyPressedEvent& e) {
  auto keyIndex = static_cast<uint16_t>(e.keyCode);
  if (keyIndex < m_keyStates.size()) {
    m_keyStates[keyIndex] = true;
  }
}

void InputSystem::onKeyReleased(KeyReleasedEvent& e) {
  auto keyIndex = static_cast<uint16_t>(e.keyCode);
  if (keyIndex < m_keyStates.size()) {
    m_keyStates[keyIndex] = false;
  }
}

void InputSystem::onMouseButtonPressed(MouseButtonPressedEvent& e) {
  uint8_t buttonIndex = static_cast<uint8_t>(e.button);
  if (buttonIndex < m_mouseButtonStates.size()) {
    m_mouseButtonStates[buttonIndex] = true;
  }
}

void InputSystem::onMouseButtonReleased(MouseButtonReleasedEvent& e) {
  uint8_t buttonIndex = static_cast<uint8_t>(e.button);
  if (buttonIndex < m_mouseButtonStates.size()) {
    m_mouseButtonStates[buttonIndex] = false;
  }
}

void InputSystem::onMouseMoved(MouseMovedEvent& e) {
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
