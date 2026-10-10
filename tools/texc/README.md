# texc

Packs the image(s) named by a texture definition (`.tex`) into a texture package (`.texpack`, `ailo/render/texture/TexturePackage.h`)
that the engine loads without decoding anything. Images are decoded with stb_image and always stored as 4 channels.

```
texc -o <output.texpack> [--depfile <file>] <input.tex>
texc --dump <package.texpack>
```

- `--depfile` lists the `.tex` file and every source image, so editing an image repacks it.
- `--dump` prints a package's header: type, format, size, levels, mipmap generation, data size.

In CMake: `add_texture(ailo assets/<dir>/<name>.tex)` produces `<build>/assets/<dir>/<name>.texpack`. The `.tex` file itself
is not copied into the build.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "bricks_albedo.png",
    "srgb": true,
    "mipmaps": true
}
```

| Property  | Type             | Default | Meaning |
|-----------|------------------|---------|---------|
| `source`  | string           |         | Image path, relative to the `.tex` file. For a cubemap, the base path of the six faces: `sky.jpg` reads `sky_px.jpg`, `sky_nx.jpg`, `sky_py.jpg`, `sky_ny.jpg`, `sky_pz.jpg`, `sky_nz.jpg`. |
| `faces`   | array of 6 strings |       | Cubemap only, instead of `source`: the faces in the order +x, -x, +y, -y, +z, -z. |
| `type`    | `"2d"` \| `"cubemap"` | `"2d"` | Cubemap faces must be square and all the same size and format. |
| `srgb`    | bool             | `true`  | 8-bit images are stored as `rgba8_srgb`, or `rgba8` when false (normal, roughness/metallic, data maps). |
| `mipmaps` | bool             | `true`  | Only level 0 is stored; the engine generates the mip chain at load time. |

HDR sources (`.hdr`) are stored as `rgba32f` whatever `srgb` says.
