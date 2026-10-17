# resgen

Embeds binary files into the engine executable, like Filament's resgen. It is for built-in resources: files the
engine needs that are not assets loaded by key (built-in shaders, the DFG LUT).

```
resgen --package <name> -o <output folder> <symbol>=<file>...
```

It writes `<output folder>/<name>.h` and `<name>.cpp`. The header declares one span per file:

```cpp
namespace ailo::resources::<name> {
extern const std::span<const uint8_t> <symbol>;
}
```

The `.cpp` holds all files in one `uint8_t` array (MSVC has neither `.incbin` nor `#embed` and limits string literals
to 64 KB). Each file starts at a multiple of 16 bytes, so SPIR-V and other word-sized data can be read in place.
Symbols must be C++ identifiers.

CMake runs it through `add_resources` (`CMakeLists.txt`):

```cmake
add_resources(ailo PACKAGE builtin_shaders RESOURCES kTonemapVert=${TONEMAP_VERT} ...)
```

which writes into `<build>/generated/resources/`, so code includes `"resources/builtin_shaders.h"`. The package is
regenerated when any of its files or resgen changes.
