#include "InputSystem.h"
#include <utils/Utils.h>
#include <utility>

namespace ailo {

void InputSystem::processEvents() {
    // Reset mouse delta each frame
    m_mouseDeltaX = 0.0;
    m_mouseDeltaY = 0.0;
    m_keysPressedThisFrame = {};
    m_keysReleasedThisFrame = {};

    Event event;
    while (pollEvent(event)) {
        onEvent(event);
    }
}

bool InputSystem::pollEvent(Event& outEvent) {
    if (m_eventQueue.empty()) {
        return false;
    }

    outEvent = m_eventQueue.front();
    m_eventQueue.pop();
    return true;
}

void InputSystem::onEvent(Event& event) {
    std::visit(utils::overloaded {
      [this](KeyPressedEvent& e)           { onKeyPressed(e);           notify(e); },
      [this](KeyReleasedEvent& e)          { onKeyReleased(e);          notify(e); },
      [this](MouseButtonPressedEvent& e)   { onMouseButtonPressed(e);   notify(e); },
      [this](MouseButtonReleasedEvent& e)  { onMouseButtonReleased(e);  notify(e); },
      [this](MouseMovedEvent& e)           { onMouseMoved(e);           notify(e); },
      [this](auto& e)                      { notify(e); }
    }, event);
}

void InputSystem::onKeyPressed(KeyPressedEvent& e) {
  auto keyIndex = static_cast<uint16_t>(e.keyCode);
  if (keyIndex < m_keyStates.size()) {
    m_keyStates[keyIndex] = true;
    m_keysPressedThisFrame[keyIndex] = true;
  }
}

void InputSystem::onKeyReleased(KeyReleasedEvent& e) {
  auto keyIndex = static_cast<uint16_t>(e.keyCode);
  if (keyIndex < m_keyStates.size()) {
    m_keyStates[keyIndex] = false;
    m_keysReleasedThisFrame[keyIndex] = true;
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
  // The first event has no previous position, so it must not report a jump from (0, 0)
  if (!m_hasMousePosition) {
    m_mouseX = e.x;
    m_mouseY = e.y;
    m_hasMousePosition = true;
  }

  e.deltaX = e.x - m_mouseX;
  e.deltaY = e.y - m_mouseY;
  m_mouseX = e.x;
  m_mouseY = e.y;

  m_mouseDeltaX += e.deltaX;
  m_mouseDeltaY += e.deltaY;
}

bool InputSystem::isKeyPressed(KeyCode key) const {
  uint16_t keyIndex = static_cast<uint16_t>(key);
  if (keyIndex >= m_keyStates.size()) {
    return false;
  }
  return m_keyStates[keyIndex];
}

bool InputSystem::pressedThisFrame(KeyCode key) const {
  uint16_t keyIndex = static_cast<uint16_t>(key);
  if (keyIndex >= m_keysPressedThisFrame.size()) {
    return false;
  }
  return m_keysPressedThisFrame[keyIndex];
}

bool InputSystem::releasedThisFrame(KeyCode key) const {
  uint16_t keyIndex = static_cast<uint16_t>(key);
  if (keyIndex >= m_keysReleasedThisFrame.size()) {
    return false;
  }
  return m_keysReleasedThisFrame[keyIndex];
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
    m_eventQueue.emplace(std::move(event));
}

} // namespace ailo
