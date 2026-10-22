#!/usr/bin/env python3
"""Writes package_<package>.ninja: builds a list of files like gen.py, then embeds them into a C++ package with bin2cpp.

The files are listed one per line in the --files file, relative to --root. Each is compiled by the tool its extension
maps to in the config, or copied, into <build>/package_build/<package>/files/, and the last edge runs bin2cpp on all of
them, sorted by path, to write <output>/<package>.h and <package>.cpp. Symbols come from the built file's path relative
to that folder (materials/toon.matpack -> <namespace>::kMaterialsToon). The manifest regenerates itself when the list
changes. See README.md.
"""

import argparse
import sys
from pathlib import Path

# Keeps Python from writing build/__pycache__ for gen.
sys.dont_write_bytecode = True

from gen import EXE_SUFFIX, escape_path, escape_value, load_json, posix, quote


def read_file_list(path, root):
    """Returns the listed files as (path relative to root, absolute path), in order."""
    files = []
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            file = (root / line).resolve()
            if not file.is_file():
                sys.exit(f"gen_package.py: {posix(file)}: no such file")
            if not file.is_relative_to(root):
                sys.exit(f"gen_package.py: {posix(file)} is not in the root {posix(root)}")
            files.append((posix(file.relative_to(root)), file))
    if not files:
        sys.exit(f"gen_package.py: {posix(path)} lists no files")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--package", required=True, help="name of the generated files")
    parser.add_argument("--namespace", help="namespace of the generated spans (default: the package name)")
    parser.add_argument("--output", help="folder of the generated .h/.cpp (default: <build-dir>/generated/packages)")
    parser.add_argument("--root", required=True, help="the listed files are relative to it, and so are the names")
    parser.add_argument("--files", required=True, help="file listing the files to embed, one per line")
    parser.add_argument("--build-dir", required=True, help="where the manifest is written and files are built")
    parser.add_argument("--bin2cpp", required=True, help="the bin2cpp executable")
    parser.add_argument("--config", help="extension -> tool config (JSON); without it every file is embedded as is")
    parser.add_argument("--tools-dir", help="folder with the tools named in the config")
    parser.add_argument("--align", type=int, help="bin2cpp --align (default: bin2cpp's)")
    args = parser.parse_args()

    if not args.package.isidentifier() or not args.package.isascii():
        parser.error("--package must be a C++ identifier")
    if args.namespace and not all(p.isidentifier() and p.isascii() for p in args.namespace.split("::")):
        parser.error("--namespace must be identifiers separated by ::")
    if args.config and not args.tools_dir:
        parser.error("--config needs --tools-dir")

    root = Path(args.root).resolve()
    files_path = Path(args.files).resolve()
    build_dir = Path(args.build_dir).resolve()
    bin2cpp = Path(args.bin2cpp).resolve()
    output = Path(args.output).resolve() if args.output else build_dir / "generated" / "packages"
    package = args.package
    script = Path(__file__).resolve()
    gen_script = script.with_name("gen.py")
    # Relative to the build folder, where ninja runs.
    work_dir = f"package_build/{package}"
    files_dir = f"{work_dir}/files"
    manifest = f"package_{package}.ninja"

    tool_configs = {}
    config_path = None
    if args.config:
        config_path = Path(args.config).resolve()
        tool_configs = load_json(config_path).get("tools", {})
    tools_dir = Path(args.tools_dir).resolve() if args.tools_dir else None
    tools = {name: posix(tools_dir / (name + EXE_SUFFIX)) for name in tool_configs}
    by_extension = {}
    for name, t in tool_configs.items():
        for ext, out_ext in t["extensions"].items():
            ext = ext.lower()
            if ext in by_extension:
                sys.exit(f"gen_package.py: {ext} is compiled by both {by_extension[ext][0]} and {name}")
            by_extension[ext] = (name, out_ext)

    regen_command = [posix(sys.executable), posix(script), "--package", package, "--output", posix(output),
                     "--root", posix(root), "--files", posix(files_path), "--build-dir", posix(build_dir),
                     "--bin2cpp", posix(bin2cpp)]
    if args.namespace:
        regen_command += ["--namespace", args.namespace]
    if config_path:
        regen_command += ["--config", posix(config_path), "--tools-dir", posix(tools_dir)]
    if args.align is not None:
        regen_command += ["--align", str(args.align)]

    bin2cpp_command = [posix(bin2cpp), "-p", package, "-o", posix(output), "-r", files_dir]
    if args.namespace:
        bin2cpp_command += ["-n", args.namespace]
    if args.align is not None:
        bin2cpp_command += ["--align", str(args.align)]
    bin2cpp_command.append(f"@{work_dir}/files.rsp")

    lines = [
        f"# Generated by {posix(script)}. Do not edit.",
        "ninja_required_version = 1.10",
        # Its own log and deps: sharing them with build_assets.ninja would make cleandead delete the other's outputs.
        f"builddir = {work_dir}",
        f"root = {escape_value(quote(posix(root)))}",
        "",
        "rule regen",
        f"  command = {escape_value(' '.join(quote(a) for a in regen_command))}",
        f"  description = Regenerating {manifest}",
        "  generator = 1",
        "",
        "rule copy",
        # See gen.py.
        f"  command = {escape_value(quote(posix(sys.executable)))} -c \"import shutil,sys;shutil.copyfile(*sys.argv[1:])\" $in $out",
        "  description = Copying $name",
        "",
        "rule bin2cpp",
        f"  command = {escape_value(' '.join(quote(a) for a in bin2cpp_command))}",
        # A response file, so a long list of files doesn't exceed the command line limit on Windows.
        f"  rspfile = {work_dir}/files.rsp",
        "  rspfile_content = $in_newline",
        f"  description = Embedding {package}",
        # bin2cpp keeps unchanged outputs' mtime, so rebuilding a file to the same bytes recompiles nothing.
        "  restat = 1",
        "",
    ]

    for name, t in sorted(tool_configs.items()):
        command = " ".join([escape_value(quote(tools[name]))] + t.get("args", ["$in", "$out"]))
        lines += [f"rule compile_{name}", f"  command = {command}"]
        if "depfile" in t:
            lines += [f"  depfile = {t['depfile']}", "  deps = gcc"]
        lines += [f"  description = {t.get('description', name)} $name", ""]

    regen_inputs = [posix(script), posix(gen_script), posix(files_path)]
    if config_path:
        regen_inputs.append(posix(config_path))
    outputs = {}

    for rel, path in read_file_list(files_path, root):
        compiled = by_extension.get(path.suffix.lower())
        if compiled:
            name, out_ext = compiled
            out = f"{files_dir}/{posix(Path(rel).with_suffix(out_ext))}"
            edge = f"compile_{name} {escape_path(posix(path))} | {escape_path(tools[name])}"
        else:
            out = f"{files_dir}/{rel}"
            edge = f"copy {escape_path(posix(path))}"
        if out in outputs:
            sys.exit(f"gen_package.py: {rel} and {outputs[out]} both build {out}")
        outputs[out] = rel
        lines.append(f"build {escape_path(out)}: {edge}")
        lines.append(f"  name = {escape_value(rel)}")
    lines.append("")

    # Sorted, so the order in the package doesn't depend on the order of the list.
    package_outputs = [posix(output / f"{package}.h"), posix(output / f"{package}.cpp")]
    lines.append(f"build {' '.join(escape_path(p) for p in package_outputs)}: bin2cpp "
                 + " ".join(escape_path(out) for out in sorted(outputs))
                 + f" | {escape_path(posix(bin2cpp))}")
    lines.append("")

    lines.append(f"build {manifest}: regen | " + " ".join(escape_path(p) for p in regen_inputs))
    for p in regen_inputs:
        lines.append(f"build {escape_path(p)}: phony")
    lines.append("")
    lines.append(f"default {' '.join(escape_path(p) for p in package_outputs)}")
    lines.append("")

    # Always rewritten, even when unchanged: ninja decides a generator edge is done by the manifest's mtime.
    build_dir.mkdir(parents=True, exist_ok=True)
    with open(build_dir / manifest, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
