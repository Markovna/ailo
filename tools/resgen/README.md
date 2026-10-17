# resgen

Embeds binary files into the engine executable. It is for built-in resources: files the
engine needs that are not assets loaded by key (built-in shaders, the DFG LUT).

```
resgen --package <name> -o <output folder> <file>...
```

It writes `<output folder>/<name>.h` and `<name>.cpp`. The header declares one span per file:

```cpp
namespace ailo::resources::embedded {
extern const std::span<const uint8_t> kTonemapVert;
}
```

The symbol is made from the file name: the last extension is dropped, the remaining letters and digits
are split into words at every other character, and each word is capitalized behind a `k` (`tonemap.vert.spv` ->
`kTonemapVert`, `dfg_lut.texpack` -> `kDfgLut`). Two files with the same symbol in a package are an error; every
package shares the namespace, so the same symbol in two packages fails to link.

The `.cpp` holds all files in one `uint8_t` array (MSVC has neither `.incbin` nor `#embed` and limits string literals
to 64 KB). Each file starts at a multiple of 16 bytes, so SPIR-V and other word-sized data can be read in place.

CMake runs it through `add_resources` (`CMakeLists.txt`):

```cmake
add_shader(BUILTIN_SHADERS shaders/tonemap.vert)
add_resources(ailo PACKAGE builtin_shaders FILES ${BUILTIN_SHADERS})
```

which writes into `<build>/generated/resources/`, so code includes `"resources/builtin_shaders.h"`. The package is
regenerated when any of its files or resgen changes.
