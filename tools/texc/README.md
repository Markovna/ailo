# texc

Packs the image(s) named by a texture definition (`.tex`) into a texture package (`.texpack`, `ailo/render/texture/TexturePackage.h`)
that the engine loads without decoding anything. Images are decoded with stb_image and always stored as 4 channels.

```
texc -o <output.texpack> [--depfile <file>] [--root <assets folder>] <input.tex>
texc --dump <package.texpack>
```

- `--root` is the assets folder that paths starting with `/` are relative to. The asset build passes it.
- `--depfile` lists the `.tex` file and every source image, so editing an image repacks it.
- `--dump` prints a package's header: type, format, size, levels, mipmap generation, data size.

It is run by the asset build (`build/README.md`): every `.tex` file in `assets/` (`assets/<dir>/<name>.tex`) is packed into `<build>/assets/<dir>/<name>.texpack`. Its source images are copied
into the build too, like any other file.

The engine loads it by the asset's name without `.tex`: `assets/textures/rock.png.tex` builds `rock.png.texpack`, loaded
with `server.load<Texture>("assets/textures/rock.png")`. `TextureLoader` reads `<key>.texpack`, or decodes the image at
`<key>` if there is no package.

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
| `source`  | string           |         | Image path, relative to the `.tex` file, or to the assets folder if it starts with `/` (`"/textures/yokohama/yokohama.jpg"`). For a cubemap, the base path of the six faces: `sky.jpg` reads `sky_px.jpg`, `sky_nx.jpg`, `sky_py.jpg`, `sky_ny.jpg`, `sky_pz.jpg`, `sky_nz.jpg`. |
| `faces`   | array of 6 strings |       | Cubemap only, instead of `source`: the faces in the order +x, -x, +y, -y, +z, -z. Paths as for `source`. |
| `type`    | `"2d"` \| `"cubemap"` | `"2d"` | Cubemap faces must be square and all the same size and format. |
| `srgb`    | bool             | `true`  | 8-bit images are stored as `rgba8_srgb`, or `rgba8` when false (normal, roughness/metallic, data maps). |
| `mipmaps` | bool             | `true`  | Only level 0 is stored; the engine generates the mip chain at load time. |

HDR sources (`.hdr`) are stored as `rgba32f` whatever `srgb` says.
