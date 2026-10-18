# texc

Packs the image(s) named by a texture definition (`.tex`) into a texture package (`.texpack`, `ailo/render/texture/TexturePackage.h`)
that the engine loads without decoding anything. Images are decoded with stb_image as 4 channels, then block-compressed (see [Formats](#formats)).

```
texc -o <output.texpack> [--depfile <file>] [--root <assets folder>] [--target desktop] <input.tex>
texc --dump <package.texpack>
```

- `--root` is the assets folder that paths starting with `/` are relative to. The asset build passes it.
- `--target` is the GPU family whose format table is used (default and only value: `desktop`).
- `--depfile` lists the `.tex` file and every source image, so editing an image repacks it.
- `--dump` prints a package's header: type, format, size, stored levels, runtime mipmap generation, data size.

It is run by the asset build (`build/README.md`): every `.tex` file in `assets/` (`assets/<dir>/<name>.tex`) is packed into `<build>/assets/<dir>/<name>.texpack`. Its source images are copied
into the build too, like any other file.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "bricks_normal.png",
    "usage": "normal",
    "quality": "high"
}
```

| Property  | Type             | Default | Meaning |
|-----------|------------------|---------|---------|
| `source`  | string           |         | Image path, relative to the `.tex` file, or to the assets folder if it starts with `/` (`"/textures/yokohama/yokohama.jpg"`). For a cubemap, the base path of the six faces: `sky.jpg` reads `sky_px.jpg`, `sky_nx.jpg`, `sky_py.jpg`, `sky_ny.jpg`, `sky_pz.jpg`, `sky_nz.jpg`. |
| `faces`   | array of 6 strings |       | Cubemap only, instead of `source`: the faces in the order +x, -x, +y, -y, +z, -z. Paths as for `source`. |
| `type`    | `"2d"` \| `"cubemap"` | `"2d"` | Cubemap faces must be square and all the same size and format. |
| `layout`  | `"faces"` \| `"equirectangular"` | `"faces"` | Cubemap only. `equirectangular`: `source` is one equirectangular (latitude/longitude) image, resampled into the six faces with the mapping `lightc` prefilters with, so a skybox matches the reflections baked from the same image. |
| `size`    | int              | width / 4 | Equirectangular only: the face size. |
| `usage`   | `"color"` \| `"normal"` \| `"hdr"` | `"hdr"` for an HDR source (`.hdr`), else `"color"` | What the texture holds, which picks its format (below). `hdr` needs an HDR source, and an HDR source needs `hdr`. |
| `quality` | `"low"` \| `"normal"` \| `"high"` | `"normal"` | Picks the format (below) and the encoder's effort. |
| `alpha`   | bool             | detected | Color only. Whether the texture uses alpha. Unset, texc scans the pixels: alpha is used if any texel's alpha is below 255. `false` drops the alpha of an image that has some. |
| `srgb`    | bool             | `true`  | Color only. Whether the color is sRGB. `false` for linear data (metallic/roughness, masks). |
| `format`  | `"bc1"` \| `"bc3"` \| `"bc4"` \| `"bc5"` \| `"bc6h"` \| `"bc7"` \| `"rgba8"` \| `"rgba16f"` \| `"rgba32f"` | | Overrides the format table. The sRGB variant is picked from `srgb`; a format without one (`bc4`, `bc5`) needs `"srgb": false`. `bc6h`, `rgba16f` and `rgba32f` need `"usage": "hdr"`, the others need an LDR source. |
| `mipmaps` | bool \| `"offline"` | `true`  | `true`: the engine generates the mip chain at load time, except for block-compressed formats, which it can't filter: for those texc generates the chain, as with `"offline"`. `"offline"`: texc box-filters the whole chain (sRGB color in linear space, each face of a cubemap on its own) and stores every level. `false`: level 0 only. |

## Formats

Textures are block-compressed with the encoders of [basis_universal](https://github.com/BinomialLLC/basis_universal)
(submodule `third_party/basis_universal`, pinned to v2.50, only linked by texc). The format is chosen from the
texture's intent, not named in the `.tex` file: `TextureFormats.cpp` holds one table per target (`--target`, only
`desktop` today), and the first row that matches the usage, quality and alpha wins. Color formats get their sRGB
variant when `srgb` is true.

| Target    | Usage    | Quality         | Alpha   | Format |
|-----------|----------|-----------------|---------|--------|
| `desktop` | `color`  | `low`, `normal` | no      | `bc1` |
| `desktop` | `color`  | `low`, `normal` | yes     | `bc3` |
| `desktop` | `color`  | `high`          | any     | `bc7` |
| `desktop` | `normal` | any             |         | `bc5` (x and y; the shader rebuilds z) |
| `desktop` | `hdr`    | any             |         | `bc6h` |

The quality also sets the effort: BC1 uses basisu's high and higher quality modes at `normal` and `high`; BC7 uses
the real-time bc7f packer at `low`, and bc7e at its fast and slow levels at `normal` and `high`; BC6H tries more
endpoint modes and partitions as the quality rises. BC4/BC5 have one setting.

A normal map stores only x and y in RG, so materials decode it with `unpackNormalMap(texel)`
(`shaders/surface/surface_fragment.fs`), which rebuilds z. The engine requires the `textureCompressionBC` device
feature.
