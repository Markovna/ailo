#!/usr/bin/env python3
"""Runs ninja on an asset manifest, then ninja -t cleandead, rebuilding the outputs of every input whose file changed.

Given an assets folder (--source, --config, --tools-dir), it first writes build_assets.ninja with gen.py when the
manifest is missing or was generated with other arguments.

Ninja only rebuilds an output older than one of its inputs, so an input replaced by a file with an older mtime (a
renamed, copied or restored file) is missed. This records each input's mtime and size after the build and, on the next
run, deletes the outputs of every input whose mtime or size differs, so ninja rebuilds them. See README.md.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import gen

# Edges that are never deleted: the manifest's own and the phony ones.
SKIPPED_RULES = {"phony", "regen"}
# Targets passed to one ninja -t query, under the Windows command line limit.
QUERY_CHUNK = 100


def ninja_tool(ninja, manifest, tool, *targets):
    result = subprocess.run([ninja, "-f", manifest, "-t", tool, *targets], capture_output=True, text=True, check=True)
    return result.stdout.splitlines()


def read_graph(ninja, manifest):
    """Returns {output: [inputs]}: the explicit and implicit inputs of each edge plus the ones its depfile recorded."""
    outputs = []
    for line in ninja_tool(ninja, manifest, "targets", "all"):
        path, _, rule = line.rpartition(": ")
        if rule not in SKIPPED_RULES:
            outputs.append(path)
    graph = {out: [] for out in outputs}

    # "out:" / "  input: rule" / "    path" or "    | path" / "  outputs:" / "    path"
    for i in range(0, len(outputs), QUERY_CHUNK):
        current, in_inputs = None, False
        for line in ninja_tool(ninja, manifest, "query", *outputs[i:i + QUERY_CHUNK]):
            if not line.startswith(" "):
                current = line.rstrip(":")
            elif line.startswith("  ") and not line.startswith("    "):
                in_inputs = line.startswith("  input:")
            elif in_inputs and current in graph:
                graph[current].append(line.strip().lstrip("|").lstrip())

    # "out: #deps N, deps mtime T (VALID)" / "    path"
    current = None
    for line in ninja_tool(ninja, manifest, "deps"):
        if not line.strip():
            continue
        if not line.startswith(" "):
            current = line.partition(": #deps")[0]
        elif current in graph:
            graph[current].append(line.strip())
    # A depfile usually lists the explicit input again.
    return {out: list(dict.fromkeys(deps)) for out, deps in graph.items()}


def signature(path):
    try:
        st = os.stat(path)
    except OSError:
        return None
    return [st.st_mtime_ns, st.st_size]


def load_state(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def invalidate(state):
    """Deletes the outputs that have an input whose signature changed since the last build."""
    inputs = state.get("inputs", {})
    changed = {path for path, sig in inputs.items() if signature(path) not in (None, sig)}
    if not changed:
        return
    for out, deps in state.get("outputs", {}).items():
        if any(dep in changed for dep in deps):
            try:
                os.remove(out)
            except FileNotFoundError:
                pass


def save_state(path, graph):
    inputs = {}
    for deps in graph.values():
        for dep in deps:
            if dep not in inputs:
                sig = signature(dep)
                if sig is not None:
                    inputs[dep] = sig
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"inputs": inputs, "outputs": graph}, f)
    os.replace(tmp, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ninja", required=True, help="ninja executable")
    parser.add_argument("--manifest", default=gen.MANIFEST,
                        help="manifest, relative to the build folder (the working directory)")
    parser.add_argument("--state", default="asset_build/inputs.json",
                        help="where the inputs' signatures are kept between builds")
    parser.add_argument("--source", help="assets folder: generates the manifest with gen.py when needed")
    parser.add_argument("--config", help="extension -> tool config (JSON), for gen.py")
    parser.add_argument("--tools-dir", help="folder with the tools named in the config, for gen.py")
    args = parser.parse_args()

    gen_args = (args.source, args.config, args.tools_dir)
    if any(gen_args):
        if not all(gen_args):
            parser.error("--source, --config and --tools-dir go together")
        if args.manifest != gen.MANIFEST:
            parser.error(f"gen.py writes {gen.MANIFEST}, not {args.manifest}")
        # After the first build ninja regenerates the manifest itself when assets are added or removed or gen.py or the
        # config change. Only a change of these arguments is left to the runner.
        if gen.read_args(args.manifest) != gen.generator_args(args.source, ".", args.config, args.tools_dir):
            gen.generate(args.source, ".", args.config, args.tools_dir)

    state_path = Path(args.state)
    state = load_state(state_path)
    if state is not None:
        invalidate(state)

    build = subprocess.run([args.ninja, "-f", args.manifest])
    if build.returncode == 0:
        subprocess.run([args.ninja, "-f", args.manifest, "-t", "cleandead"], check=True)
    # Saved after a failed build too: the deleted outputs stay missing, so they are rebuilt next time anyway. If ninja
    # can't even read the manifest, the old state is kept.
    try:
        graph = read_graph(args.ninja, args.manifest)
    except subprocess.CalledProcessError:
        sys.exit(build.returncode or 1)
    save_state(state_path, graph)
    sys.exit(build.returncode)


if __name__ == "__main__":
    main()
