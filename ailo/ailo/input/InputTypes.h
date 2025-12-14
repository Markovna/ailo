#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace ailo {

enum class KeyCode : uint16_t {
    Unknown = 0,

    // Alphanumeric keys
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Special keys
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Semicolon = 59,
    Equal = 61,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

    // Control keys
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,

    // Arrow keys
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,

    // Navigation keys
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,

    // Lock keys
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,

    // Numpad keys
    Numpad0 = 320, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal = 330,
    NumpadDivide = 331,
    NumpadMultiply = 332,
    NumpadSubtract = 333,
    NumpadAdd = 334,
    NumpadEnter = 335,
    NumpadEqual = 336,

    // Modifier keys
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348
};

// Platform-agnostic mouse button codes
enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7
};

// Key/Button action types
enum class InputAction : uint8_t {
    Release = 0,
    Press = 1,
    Repeat = 2
};

// Modifier key flags
enum class ModifierKey : uint8_t {
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3,
    CapsLock = 1 << 4,
    NumLock = 1 << 5
};

// Bitwise operators for ModifierKey
inline ModifierKey operator|(ModifierKey a, ModifierKey b) {
    return static_cast<ModifierKey>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline ModifierKey operator&(ModifierKey a, ModifierKey b) {
    return static_cast<ModifierKey>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline ModifierKey operator^(ModifierKey a, ModifierKey b) {
    return static_cast<ModifierKey>(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
}

inline ModifierKey operator~(ModifierKey a) {
    return static_cast<ModifierKey>(~static_cast<uint8_t>(a));
}

inline ModifierKey& operator|=(ModifierKey& a, ModifierKey b) {
    return a = a | b;
}

inline ModifierKey& operator&=(ModifierKey& a, ModifierKey b) {
    return a = a & b;
}

inline ModifierKey& operator^=(ModifierKey& a, ModifierKey b) {
    return a = a ^ b;
}

inline bool hasModifier(ModifierKey mods, ModifierKey flag) {
    return static_cast<uint8_t>(mods & flag) != 0;
}

// Event structures (no inheritance, designed for std::variant)

struct KeyPressedEvent {
    KeyCode keyCode = KeyCode::Unknown;
    ModifierKey modifiers = ModifierKey::None;
};

struct KeyReleasedEvent {
    KeyCode keyCode = KeyCode::Unknown;
    ModifierKey modifiers = ModifierKey::None;
};

struct KeyRepeatedEvent {
    KeyCode keyCode = KeyCode::Unknown;
    ModifierKey modifiers = ModifierKey::None;
};

struct MouseButtonPressedEvent {
    MouseButton button = MouseButton::Left;
    ModifierKey modifiers = ModifierKey::None;
    double x = 0.0;
    double y = 0.0;
};

struct MouseButtonReleasedEvent {
    MouseButton button = MouseButton::Left;
    ModifierKey modifiers = ModifierKey::None;
    double x = 0.0;
    double y = 0.0;
};

struct MouseMovedEvent {
    double x = 0.0;
    double y = 0.0;
    double deltaX = 0.0;
    double deltaY = 0.0;
};

struct MouseScrolledEvent {
    double xOffset = 0.0;
    double yOffset = 0.0;
};

// Event variant type
using Event = std::variant<
    KeyPressedEvent,
    KeyReleasedEvent,
    KeyRepeatedEvent,
    MouseButtonPressedEvent,
    MouseButtonReleasedEvent,
    MouseMovedEvent,
    MouseScrolledEvent
>;

} // namespace ailo
