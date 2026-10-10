# Asset build

Builds `assets/` into `<build>/assets/` with Ninja. Each asset is compiled by its tool. Everything else is copied.

- **Asset file:** a JSON file (comments allowed) in `assets/` with a `"__type"` property, e.g. `assets/textures/sky.tex`:
  `{ "__type": "texture", "type": "cubemap", "source": "sky.jpg" }`. The extension is up to you.
- **Config** (`config.json`): maps each asset type to the tool that compiles it. An asset whose type is not listed is
  copied, like any file that is not an asset.
- **Generator** (`gen.py`): scans `assets/` and writes `<build>/build_assets.ninja`, one edge per file.
- **Ninja** compares timestamps and runs only what is out of date, on all cores. Its log and deps live in
  `<build>/asset_build/`, apart from CMake's.
- **Tools** (e.g. `texc`) compile a single asset and can write a depfile with the extra inputs they read (source
  images, includes), so editing one of those rebuilds the asset.

CMake runs `gen.py` once, when `build_assets.ninja` is missing or `gen.py`/`config.json` change. The `ailo_assets`
target (a dependency of `ailo`) runs `ninja -f build_assets.ninja` and then `ninja -t cleandead`, which deletes the
outputs of assets that no longer exist. The manifest depends on every folder in `assets/` and every JSON file in it, so
Ninja reruns `gen.py` itself when a file is added, removed, or edited.

By hand, from the build folder:

```
ninja -f build_assets.ninja
```

## Config

Strict JSON with `//` and `/* */` comments.

```
{
    "types": {
        "texture": {
            "tool": "texc",
            "output": ".texpack",
            "args": ["--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d",
            "description": "Packing texture"
        }
    }
}
```

| Property                | Meaning |
|-------------------------|---------|
| `types.<t>.tool`        | Tool name. Its path is passed to `gen.py` as `--tool <name>=<path>` (CMake: `--tool texc=$<TARGET_FILE:texc>`). Assets are rebuilt when the tool changes. |
| `types.<t>.output`      | Output extension. It replaces the asset's extension: `textures/sky.tex` builds `<build>/assets/textures/sky.texpack`. |
| `types.<t>.args`        | Tool arguments. Ninja variables: `$in` is the asset file (absolute), `$out` is the output (relative to the build folder). Default: `["$in", "$out"]`. |
| `types.<t>.depfile`     | Optional. The Makefile-style depfile the tool writes. Its target must be `$out` as passed. |
| `types.<t>.description` | Optional. Printed before the asset's path. |

## Adding a type

1. Write the tool. It takes one asset file and writes one output, plus a depfile if it reads other files.
2. Add it to `types` in `config.json`. Add `--tool <name>=$<TARGET_FILE:<name>>` to the `gen.py` command in
   `CMakeLists.txt`, and add `add_dependencies(ailo_assets <name>)`.
