# modelc

Compiles a model definition (`.model`) into a model package, which keeps the definition's name (`.model`). The source model (glTF, FBX, OBJ,
anything assimp reads) is imported with assimp.

```
modelc -o <output.model> [--depfile <file>] [--root <assets folder>] <input.model>
modelc --dump <package.model>
```

- `--root` is the assets folder that a `source` starting with `/` is relative to. The asset build passes it.
- `--depfile` lists the `.model` file and every file assimp opened (the source, `.bin` buffers, `.mtl` libraries,
  ...).
- `--dump` prints the package's meshes (with meshoptimizer's ACMR and ATVR for a 16-entry cache, overfetch and
  overdraw), materials with their parameters, instances, skeleton, skins, clips with their sync markers and embedded
  textures.

## Format

Strict JSON (quoted keys and strings); `//` and `/* */` comments are allowed. Unknown properties are errors.

```
{
    "source": "model.gltf",
    "material": "default.mat",
    "materials": { "Curtain": "cloth.mat" },
    "textures": { "rock.png": "rock.png.tex" }
}
```

| Property    | Type   | Meaning |
|-------------|--------|---------|
| `source`    | string | Required. The model file, relative to the `.model` file, or to the assets folder if it starts with `/` . |
| `material`  | string | Required. The material every mesh uses, a path like `source` or a key with a source (see [Materials](#materials)). |
| `materials` | object | Optional. Source material name -> material, written like `material`, overriding it. Naming a material the source doesn't have is an error. |
| `nodeMaterials` | object | Optional. Source node -> material for every mesh of that node, written like `material`, overriding `material` and `materials`, for meshes that share a source material (`{ "Cockpit": "glass.matinst" }`). A node is named, which matches every node of that name, or given by its path below the root, its ancestors' names joined by `/` (`"Ship/Ship"`), which tells apart nodes with the same name and wins over a name. The source's parameters are not written for it. An entry that matches no node with meshes is an error, which lists their paths. |
| `textures`  | object | Optional. Source texture URI -> texture key written instead (see [Textures](#textures)), like `materials`. Naming a URI the source doesn't reference is an error. |
| `animations` | object | Optional. Clip name -> file whose animations are added to the model's clips, relative like `source` (see [Animations](#animations)). |
| `optimize`  | bool   | Optional, default `true`. Optimizes the meshes with meshoptimizer (see [Geometry](#geometry)); `false` keeps assimp's vertex and triangle order. |
| `footMarkers` | object | Optional. Generates sync markers where the feet come down (see [Foot markers](#foot-markers)). |

## Materials

A material is a path, relative to the `.model` file or to the assets folder if it starts with `/`, like `source`, or a
key with a source (`engine:/materials/lit.mat`, `builtin://...`), which is written as it is. Paths are written relative
to the `.model` file, so `"/materials/lit.mat"` in `models/rock.model` is written as `../materials/lit.mat`; they are
not checked to exist. Absolute paths are errors.

modelc never reads the materials: it writes their paths. A path ending in `.matinst` names a material instance
(`tools/matc/README.md`), which the runtime uses as it is: the parameters below are not applied to it, so it suits
sources whose materials carry nothing useful (an FBX without texture paths). It writes these parameters when the source has them, whether
or not the material declares them:

| Parameter              | From |
|------------------------|------|
| `baseColorMap`         | The base color texture, or the diffuse texture. |
| `normalMap`            | The normal texture. |
| `metallicRoughnessMap` | The glTF metallic/roughness texture, or the key `builtin://textures/default_metallic_roughness` if the source has no metallic factor (non-PBR formats). |
| `baseColorFactor`      | The base color. |
| `metallicFactor`, `roughnessFactor` | The glTF factors. |

## Textures

- **Unnamed** texture slots (some exporters write a slot with an empty file name) are skipped.
- **Embedded** textures are packed into the model package, sRGB for base color and linear for the other maps.
- **Referenced by URI**, the package stores a reference: the URI (percent-decoded for glTF) relative to the source
  model, rewritten relative to the `.model` file, with its own extension. With `"source": "model.gltf"`, `white.png`
  is stored as `white.png`. modelc doesn't look at the texture and adds no build dependency on it. Absolute paths are
  errors.
- **Overridden** by `textures`, keyed by the URI as the source writes it (percent-decoded for glTF): the reference is
  replaced by the whole path given, not just its file name, relative to the `.model` file, or to the assets folder if
  it starts with `/`, and stored as given (normalized), extension included: `"white.png": "white.png.tex"` refers
  to the texture texc builds from `white.png.tex`. modelc doesn't check that it exists. An overridden URI may be
  absolute in the source.

## Animations

`animations` adds clips from other files, typically one FBX per clip exported without a mesh. Only their
animations are read: their channels animate the source's skeleton by node name, so they must come from the same
skeleton, and channels for nodes the skeleton doesn't have are ignored at runtime. A file with one animation adds a clip
with the given name; a file with several adds `<name>/<animation name>` for each. Every file is listed in the depfile.
It is an error if the source has no skinned mesh or a file has no animations.

```
{
    "source": ".source/character.fbx",
    "material": "engine:/materials/lit.mat",
    "animations": { "walk": "/animations/.source/walk.fbx", "jump": "/animations/.source/jump.fbx" }
}
```

## Foot markers

`footMarkers` puts sync markers on clips at the moments each foot comes down, the way Unreal's footstep animation
modifier does. The runtime builds each clip's `SyncTrack` from its markers (`ailo/animation/SyncTrack.h`): each marker
starts a sync event, named after the marker, that lasts until the next marker. A clip with no markers gets one sync event
over its whole length.

```
"footMarkers": {
    "feet": { "LeftFoot": "foot_l", "RightFoot": "foot_r" },
    "clips": [ "walk", "jog" ],
    "groundThreshold": 5
}
```

| Property          | Type   | Meaning |
|-------------------|--------|---------|
| `feet`            | object | Required. Marker name -> foot bone. A bone the skeleton doesn't have is an error. |
| `groundThreshold` | number | Required. How far above its lowest point in the clip a foot still counts as planted, in source units (centimeters for Unreal FBX files). |
| `speedThreshold`  | number | Optional, default 0 (off). A planted foot must also move slower than this, in source units per second. This only works for clips that keep their root motion, where a planted foot stands still. |
| `sampleRate`      | number | Optional, default 60. Samples per second. Markers fall on samples. |
| `up`              | array  | Optional, default `[0, 1, 0]`. The up axis in the model's space, after assimp's import. |
| `clips`           | array  | Optional. The clips to mark, by name. All clips if left out. Naming a clip the model doesn't have is an error. A listed clip that gets no markers is a warning. |

Each foot bone is sampled in model space, its ancestors included. A foot comes down at a sample where it is planted and
was not planted at the sample before. The first sample follows the last, as the clip loops. A foot that is planted for the
whole clip, or never, adds no markers, so an idle clip keeps the default track. When two feet come down at the same
sample, the marker whose name sorts first is kept. A clip can have at most `SyncTrack::kMaxEvents` markers. If an ankle bobs above the
threshold while the foot is planted (a heel-to-toe roll), the foot gets two markers per step: raise `groundThreshold`.
`--dump` lists each clip's markers, which is how to check the settings.

## Geometry

Meshes are triangulated; points and lines are dropped. Indices are stored 16-bit when the mesh has at most 65536
vertices (after optimization) and 32-bit otherwise; meshes are never split.
Vertices are stored in the package's layout (`StaticVertex`, or `SkinnedVertex` with up to 4 normalized bone weights for
skinned meshes); identical vertices are merged. Unless `"optimize": false`, each mesh is then optimized with
[meshoptimizer](https://github.com/zeux/meshoptimizer): vertices identical in that layout are merged again,
triangles are reordered for the post-transform vertex cache and then for overdraw (accepting up to 5% worse cache
efficiency), and vertices are reordered by first use for vertex fetch. The node hierarchy is flattened into mesh instances with their
transforms relative to the root. A skeleton (bones and their ancestors), one skin per skinned mesh (its bones as
skeleton node indices, with their inverse bind matrices, at most 256) and animation clips are written if any mesh is
skinned. A mesh's vertices index its skin's bones.
