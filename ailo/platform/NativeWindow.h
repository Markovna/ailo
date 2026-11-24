#pragma once

namespace ailo {

struct NativeWindow {
    void* display = nullptr; // X11 only
    void* window = nullptr; // can be null
};

}
