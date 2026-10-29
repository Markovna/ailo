# Building on Windows

## Prerequisites

- [CMake](https://cmake.org/download/) 3.22 or newer
- MSVC from Visual Studio 2026, or MinGW with GCC 13 or newer
- [Python 3](https://www.python.org/downloads/), on `PATH`
- [Ninja](https://github.com/ninja-build/ninja/releases), on `PATH`

All other dependencies are included in the repository. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) only for the validation layer, which Debug builds enable when it is installed.

## Steps

1. Clone the repository with its submodules:

   ```bash
   git clone --recursive <repository-url> ailo
   cd ailo
   ```

   If you already cloned it without `--recursive`:

   ```bash
   git submodule update --init --recursive
   ```

2. Configure:

   ```bash
   cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
   ```

   With MSVC, run this and the next steps from the "x64 Native Tools Command Prompt" that Visual Studio installs, so the compiler is found. Use `cmake-build-debug` and `Debug` for a debug build.


3. Build:

   ```bash
   cmake --build cmake-build-release
   ```
   
