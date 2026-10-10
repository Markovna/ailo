# Asset build

Builds `assets/` into `<build>/assets/` with Ninja. Each asset is compiled by its tool. Everything else is copied.

- **Asset file:** a file in `assets/` whose extension is listed in the config, e.g. `assets/textures/sky.tex`, which
  `texc` packs into `<build>/assets/textures/sky.texpack`.
- **Config** (`config.json`): maps each tool to the extensions it compiles and the extension of each one's output. A
  file whose extension is not listed is copied.
- **Generator** (`gen.py`): scans `assets/` and writes `<build>/build_assets.ninja`, one edge per file.
- **Ninja** compares timestamps and runs only what is out of date, on all cores. Its log and deps live in
  `<build>/asset_build/`, apart from CMake's.
- **Tools** (e.g. `texc`) compile a single asset and can write a depfile with the extra inputs they read (source
  images, includes), so editing one of those rebuilds the asset.

CMake runs `gen.py` once, when `build_assets.ninja` is missing or `gen.py`/`config.json` change. The `ailo_assets`
target (a dependency of `ailo`) runs `ninja -f build_assets.ninja` and then `ninja -t cleandead`, which deletes the
outputs of assets that no longer exist. The manifest depends on every folder in `assets/`, so Ninja reruns `gen.py`
itself when a file is added or removed.

By hand, from the build folder:

```
ninja -f build_assets.ninja
```

## Config

Strict JSON with `//` and `/* */` comments.

```
{
    "tools": {
        "texc": {
            "extensions": { ".tex": ".texpack" },
            "args": ["--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        },
        "matcomp": {
            "extensions": { ".mat": ".matpack" },
            "args": ["--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        }
    }
}
```

| Property                   | Meaning |
|----------------------------|---------|
| `tools.<name>`             | Tool name. Its path is passed to `gen.py` as `--tool <name>=<path>` (CMake: `--tool texc=$<TARGET_FILE:texc>`). Assets are rebuilt when the tool changes. |
| `tools.<name>.extensions`  | Asset extension -> output extension, matched case-insensitively. The output extension replaces the asset's: `textures/sky.tex` builds `<build>/assets/textures/sky.texpack`. An extension may belong to only one tool. |
| `tools.<name>.args`        | Tool arguments, the same for every extension: the tool decides what to compile from `$in`'s extension. Ninja variables: `$in` is the asset file (absolute), `$out` is the output (relative to the build folder). Default: `["$in", "$out"]`. |
| `tools.<name>.depfile`     | Optional. The Makefile-style depfile the tool writes. Its target must be `$out` as passed. |
| `tools.<name>.description` | Optional. Printed before the asset's path. Default: the tool name. |

## Adding an asset kind

- **To an existing tool:** teach the tool the new extension, then add it to the tool's `extensions`, e.g.
  `{ ".tex": ".texpack", ".model": ".modelpack" }`.
- **With a new tool:** write it. It takes one asset file and writes one output, plus a depfile if it reads other files.
  Add it to `tools` in `config.json`, add `--tool <name>=$<TARGET_FILE:<name>>` to the `gen.py` command in
  `CMakeLists.txt`, and add `add_dependencies(ailo_assets <name>)`.
