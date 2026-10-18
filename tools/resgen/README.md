# resgen

Embeds binary files into a C++ source file, so a program can read them without opening any file (e.g. compiled
shaders, lookup tables).

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

To use a package, compile `<name>.cpp` into the program and include `<name>.h`. Rerun resgen when any of the files
changes.
