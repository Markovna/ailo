# Asset build

Builds `assets/` into `<build>/assets/` with Ninja. Each asset is compiled by its tool. Everything else is copied.

- **Asset file:** a file in `assets/` whose extension is listed in the config, e.g. `assets/textures/sky.tex`, which
  `texc` packs into `<build>/assets/textures/sky.texpack`.
- **Config** (`config.json`): maps each tool to the extensions it compiles and the extension of each one's output. A
  file whose extension is not listed is copied.
- **Generator** (`gen.py`): scans `assets/` and writes `<build>/build_assets.ninja`, one edge per file.
- **Ignore file** (`assets/.assetignore`): files and folders the generator skips, e.g. source images that are only
  read by a tool.
- **Ninja** compares timestamps and runs only what is out of date, on all cores. Its log and deps live in
  `<build>/asset_build/`, apart from CMake's.
- **Runner** (`run.py`): writes the manifest with the generator when it is missing or was generated with other
  arguments, then runs Ninja and `ninja -t cleandead`. Ninja misses an input replaced by an older file (renamed,
  copied or restored with its mtime), so the runner records every input's mtime and size after each build, in
  `<build>/asset_build/inputs.json`, and before the next one deletes the outputs of every input whose mtime or size
  changed, so Ninja rebuilds them.
- **Tools** (e.g. `texc`) are built by CMake into `<build>/asset_tools/`, which is passed to the generator as
  `--tools-dir`. They compile a single asset and can write a depfile with the extra inputs they read (source images,
  includes), so editing one of those rebuilds the asset.

The game's assets target runs `run.py` from the build folder with the assets folder, the config and the tools folder.
It runs `gen.py` when `build_assets.ninja` is missing or was generated with other arguments (the manifest records
them), then runs `ninja -f build_assets.ninja` and `ninja -t cleandead`, which deletes the outputs of assets that no
longer exist. The manifest depends on every folder in `assets/`, `gen.py`, the config and the ignore file, so Ninja
reruns `gen.py` itself when a file is added or removed or one of those changes.

By hand, from the build folder:

```
python <repo>/build/run.py --source <game>/assets --config <repo>/build/config.json --tools-dir asset_tools
```

`--ninja` names the Ninja executable; without it, `run.py` uses `ninja` from `PATH` and fails if there is none.
Without `--source`, `--config` and `--tools-dir`, `run.py` only runs an existing manifest, by default
`build_assets.ninja` (`--manifest`), keeping its state in `asset_build/inputs.json` (`--state`). Once the manifest
exists, plain `ninja -f build_assets.ninja` works too, but misses inputs replaced by older files.

## Config

Strict JSON with `//` and `/* */` comments.

```
{
    "tools": {
        "texc": {
            "extensions": { ".tex": ".texpack" },
            "args": ["--root", "$root", "--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        },
        "matcomp": {
            "extensions": { ".mat": ".matpack" },
            "args": ["--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        },
        "modelc": {
            "extensions": { ".model": ".modelpack" },
            "args": ["--root", "$root", "--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        },
        "lightc": {
            "extensions": { ".light": ".lightpack" },
            "args": ["--root", "$root", "--depfile", "$out.d", "-o", "$out", "$in"],
            "depfile": "$out.d"
        }
    }
}
```

| Property                   | Meaning |
|----------------------------|---------|
| `tools.<name>`             | Tool name: the executable `<name>` (`<name>.exe` on Windows) in the folder passed as `--tools-dir`. Assets are rebuilt when the tool changes. |
| `tools.<name>.extensions`  | Asset extension -> output extension, matched case-insensitively. The output extension replaces the asset's: `textures/sky.tex` builds `<build>/assets/textures/sky.texpack`. An extension may belong to only one tool. |
| `tools.<name>.args`        | Tool arguments, the same for every extension: the tool decides what to compile from `$in`'s extension. Ninja variables: `$in` is the asset file (absolute), `$out` is the output (relative to the build folder), `$root` is the assets folder (absolute; texc, modelc and lightc take it as `--root` to resolve paths starting with `/`). Default: `["$in", "$out"]`. |
| `tools.<name>.depfile`     | Optional. The Makefile-style depfile the tool writes. Its target must be `$out` as passed. |
| `tools.<name>.description` | Optional. Printed before the asset's path. Default: the tool name. |

## Ignore file

`assets/.assetignore` uses a subset of `.gitignore` syntax. One pattern per line; blank lines and lines starting with
`#` are skipped. Matching is case-insensitive, and an ignored folder is not scanned at all.

| Pattern                 | Ignores |
|-------------------------|---------|
| `*.hdr`                 | Every file or folder with that name, at any depth (`*`, `?` and `[...]` are glob wildcards). |
| `sources/`              | A trailing `/` matches folders only. |
| `textures/sky.hdr`      | A pattern containing `/` (other than a trailing one) is matched against the whole path relative to `assets/`. A leading `/` is allowed. |

Negation (`!`) and `**` are not supported. Editing the file regenerates the manifest.

## Embedded assets

`gen_package.py` builds a list of files into a C++ package instead of separate files, so a program can read them
without opening any file. It takes the files (relative to a root folder), the `bin2cpp` executable
(`tools/bin2cpp/README.md`), which writes the package, and optionally the same config and tools as `gen.py`; without them every file is embedded
as it is. The ignore file does not apply: only the listed files are built. In CMake, `embed_assets` (root
`CMakeLists.txt`) does all of it for a target:

```cmake
embed_assets(mygame PACKAGE game_assets ROOT assets FILES
        materials/toon.mat
        textures/logo.png.tex
)
```

`ROOT` defaults to the current source folder. The function writes the list to `<build>/package_build/<name>/files.txt`
(only when it changes), generates `<build>/package_<name>.ninja` with `gen_package.py` and runs it on every build, like
the asset build. The manifest compiles each file with its tool, or copies it, into `<build>/package_build/<name>/files/`
and then runs bin2cpp on all of them, sorted by path and passed in a response file, with that folder as its root. It
writes `<build>/generated/embedded/<name>.h` and `<name>.cpp`, which are compiled into the target:

```cpp
namespace ailo::embedded::game_assets {

extern const std::span<const std::uint8_t> kPackage;

// materials/toon.matpack
extern const std::span<const std::uint8_t> kMaterialsToon;
...
}
```

A file's span is named after its built path, with the output extension dropped: `materials/toon.mat` builds
`materials/toon.matpack`, `kMaterialsToon`. `kPackage` spans the whole package. Each file starts at a multiple of 16 bytes. A listed file
that is missing or outside the root, an empty list, or two files with the same output or symbol fail the generation or
bin2cpp. bin2cpp rewrites the header and source only when their content changes (`restat`), so rebuilding an asset to the
same bytes recompiles nothing. The manifest regenerates itself when the list, the config or the scripts change, and
has its own log and `run.py` state in `<build>/package_build/<name>/`. The package name must be unique in the build.

The engine reads a package with `MemoryAssetReader` (`ailo/assets/MemoryAssetReader.h`), which is given every
asset's key and span explicitly, and usually registered as a source:

```cpp
#include "embedded/game_assets.h"

server.registerReader("game", std::make_unique<MemoryAssetReader>(std::initializer_list<MemoryAssetReader::Asset> {
    { "materials/toon.matpack", embedded::game_assets::kMaterialsToon },
}));
server.load<Material>("game:/materials/toon.matpack");
```

By hand, from the repository root and then the build folder:

```
python build/gen_package.py --root <folder> --files <list> --build-dir <build> --bin2cpp <build>/tools/bin2cpp/bin2cpp.exe
                            --package <name> [--namespace <ns>] [--output <folder>] [--config build/config.json --tools-dir <build>/asset_tools]
                            [--align <n>]
ninja -f package_<name>.ninja
```

`<list>` names one file per line, relative to the root. `--namespace` defaults to the package name (`embed_assets`
passes `ailo::embedded::<name>`), `--output` to `<build>/generated/packages/`. The
source holds every byte as an array initializer, about six times the files' size, so embed only small assets.

## Adding an asset kind

- **To an existing tool:** teach the tool the new extension, then add it to the tool's `extensions`, e.g.
  `{ ".tex": ".texpack", ".cube": ".texpack" }`.
- **With a new tool:** write it. It takes one asset file and writes one output, plus a depfile if it reads other files.
  Add it to `tools` in `config.json` and its target to `AILO_ASSET_TOOLS` in `CMakeLists.txt`, which builds it into
  `<build>/asset_tools/` before the assets.
