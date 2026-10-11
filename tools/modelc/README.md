# modelc

Compiles a model definition (`.model`) into a model package (`.modelpack`, `ailo/render/model/ModelPackage.h`) that the
engine loads without importing anything. The source model (glTF, FBX, OBJ, anything assimp reads) is imported with
assimp, which only modelc links.

```
modelc -o <output.modelpack> [--depfile <file>] [--root <assets folder>] <input.model>
modelc --dump <package.modelpack>
```

- `--root` is the assets folder that a `source` starting with `/` is relative to. The asset build passes it.
- `--depfile` lists the `.model` file and every file assimp opened (the source, `.bin` buffers, `.mtl` libraries,
  ...).
- `--dump` prints the package's meshes, materials with their parameters, instances, skeleton, clips and embedded
  textures.

It is run by the asset build (`build/README.md`): `assets/<dir>/<name>.model` builds
`<build>/assets/<dir>/<name>.modelpack`. The model sources are listed in `assets/.assetignore`, so they are not copied
into the build.

The engine loads it by the asset's name without `.model`: `assets/models/sponza/Sponza.gltf.model` builds
`Sponza.gltf.modelpack`, loaded with `server.load<Model>("assets/models/sponza/Sponza.gltf")`.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "Sponza.gltf",
    "material": "assets/materials/lit",
    "materials": { "Curtain": "assets/materials/toon" }
}
```

| Property    | Type   | Meaning |
|-------------|--------|---------|
| `source`    | string | Required. The model file, relative to the `.model` file, or to the assets folder if it starts with `/` (`"/models/sponza/Sponza.gltf"`). |
| `material`  | string | Required. Asset key of the material every mesh uses. |
| `materials` | object | Optional. Source material name -> material key, overriding `material`. Naming a material the source doesn't have is an error. |

modelc never reads the materials: it writes their keys, and the runtime loads them. It writes these parameters when
the source has them, and the runtime skips (with a warning) the ones the material doesn't declare:

| Parameter              | From |
|------------------------|------|
| `baseColorMap`         | The base color texture, or the diffuse texture. |
| `normalMap`            | The normal texture. |
| `metallicRoughnessMap` | The glTF metallic/roughness texture, or the builtin default metallic/roughness texture if the source has no metallic factor (non-PBR formats). |
| `baseColorFactor`      | The base color. |
| `metallicFactor`, `roughnessFactor` | The glTF factors. |

## Textures

- **Embedded** textures are packed into the model package, sRGB for base color and linear for the other maps.
- **Referenced by URI**, the package stores a reference that the runtime resolves: the URI (percent-decoded for
  glTF) relative to the source model, written relative to the `.model` file, and loaded as
  `<model key's folder>/<reference>`. modelc doesn't look at the texture and adds no build dependency on it. With
  `"source": "Sponza.gltf"`, `white.png` is stored as `white.png`, and the runtime reads `white.png.texpack`, built
  from a `white.png.tex` file next to the `.model` file (`tools/texc/README.md`), and otherwise decodes `white.png`
  as an sRGB color image, so data textures (normal, metallic/roughness) need a `.tex` file with `"srgb": false`.
  Absolute paths are errors.

## Geometry

Meshes are triangulated and split to at most 65535 vertices (indices are 16-bit). Points and lines are dropped.
Vertices are stored in the engine's layout (`model::StaticVertex`, or `model::SkinnedVertex` with up to 4 bone weights
for skinned meshes); identical vertices are merged. The node hierarchy is flattened into mesh instances with their
transforms relative to the root. A skeleton (bones and their ancestors) and animation clips are written if any mesh is
skinned.
