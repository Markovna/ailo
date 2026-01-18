#include "Application.h"

#include <iostream>
#include <stdexcept>


template <typename T>
struct MyAlloc : std::allocator<T> {};

int main() {
    Application app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


