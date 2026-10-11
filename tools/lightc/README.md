# lightc

Bakes image-based lighting data into texture packages (`.texpack`, `ailo/render/texture/TexturePackage.h`):

- a light definition (`.light`) naming an equirectangular HDR environment becomes its prefiltered specular radiance
  cubemap;
- `--dfg` writes the DFG LUT of the split-sum approximation. It has no source, so CMake runs it, not the asset build.

```
lightc -o <output.texpack> [--depfile <file>] [--root <assets folder>] <input.light>
lightc --dfg -o <output.texpack>
```

- `--root` is the assets folder that paths starting with `/` are relative to. The asset build passes it.
- `--depfile` lists the `.light` file and its source image, so editing either rebakes it.
- `texc --dump <package.texpack>` prints the header of either output.

It is run by the asset build (`build/README.md`): every `.light` file in `assets/` (`assets/<dir>/<name>.light`) is
baked into `<build>/assets/<dir>/<name>.texpack`, loaded with `server.load<Texture>("assets/<dir>/<name>")`. The DFG LUT
is built by CMake into `<build>/assets/textures/dfg_lut.texpack` (key `assets/textures/dfg_lut`).

## Outputs

**Prefiltered radiance:** an `rgba32f` cubemap of `size` with every level stored, down to 16x16. Level `i` of `n` is
filtered with the GGX lobe of perceptual roughness `r` where `lod = i / (n - 1) = r * (2 - r)`, the mapping the shader
inverts (`iblSpecularMaxLod` is `n - 1`). Level 0 is the unfiltered environment. Each GGX sample reads a mip of the
source whose texels cover its solid angle (filtered importance sampling), so few samples don't alias.

**DFG LUT:** a 256x256 `rgba32f` 2D texture without mips. `x` is NoV, `y` is perceptual roughness, both at texel centers;
`rg` hold the multiscatter DFG terms.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "rogland_clear_night_4k.hdr",
    "size": 1024,
    "samples": 1024
}
```

| Property  | Type   | Default | Meaning |
|-----------|--------|---------|---------|
| `source`  | string |         | Equirectangular HDR image, relative to the `.light` file, or to the assets folder if it starts with `/`. Usually listed in `assets/.assetignore`, since only the baked package is loaded. |
| `size`    | int    | `1024`  | Face size of level 0, a power of two. |
| `samples` | int    | `1024`  | GGX samples per texel for the rough levels. |
