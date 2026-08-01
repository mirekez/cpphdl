#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from pathlib import Path


INCLUDE_RE = re.compile(r'^#include\s+"([^"]+)"\s*$')
ALIAS_RE = re.compile(
    r"^using\s+(cpphdl_opt_t\d+)\s*=\s*(.+);\s*$",
    re.MULTILINE,
)
ALIAS_NAME_RE = re.compile(r"\bcpphdl_opt_t\d+\b")
MODULE_RE = re.compile(
    r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*:\s*public\s+(?:cpphdl::)?Module\b"
)


def write_if_changed(path: Path, text: str) -> None:
    if path.exists() and path.read_text() == text:
        return
    path.write_text(text)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare a concrete, reduced input for cpphdl --optimize-combs"
    )
    parser.add_argument("output", type=Path)
    parser.add_argument("--collection-chunk-size", type=int, default=64)
    parser.add_argument("--collection-max-definition-bytes", type=int, default=10_000_000)
    parser.add_argument("--collection-isolate-definition-bytes", type=int, default=3_000)
    args = parser.parse_args()
    if args.collection_chunk_size < 1:
        parser.error("--collection-chunk-size must be positive")
    if args.collection_max_definition_bytes < 1:
        parser.error("--collection-max-definition-bytes must be positive")
    if args.collection_isolate_definition_bytes < 1:
        parser.error("--collection-isolate-definition-bytes must be positive")

    output = args.output.resolve()
    umbrella = output / "all_generated.h"
    externs = output / "cpphdl_optimized_externs.h"
    for required in (umbrella, externs):
        if not required.is_file():
            parser.error(f"missing generated input: {required}")

    extern_text = externs.read_text()
    aliases = ALIAS_RE.findall(extern_text)
    root_alias = next((item for item in aliases if item[0] == "cpphdl_opt_t0"), None)
    if root_alias is None:
        parser.error(f"missing concrete root alias cpphdl_opt_t0 in {externs}")
    root_module = re.match(r"([A-Za-z_]\w*)", root_alias[1])
    if root_module is None:
        parser.error(f"cannot identify concrete root module in {root_alias[1]}")
    used_modules = {root_module.group(1)}

    umbrella_lines = umbrella.read_text().splitlines()
    header_text: dict[str, str] = {}
    header_modules: dict[str, set[str]] = {}
    module_header: dict[str, str] = {}
    for line in umbrella_lines:
        match = INCLUDE_RE.match(line)
        if match is None:
            continue
        name = match.group(1)
        header = output / name
        if not header.is_file():
            continue
        text = header.read_text()
        modules = set(MODULE_RE.findall(text))
        header_text[name] = text
        header_modules[name] = modules
        for module in modules:
            module_header[module] = name

    # Alias roots omit helper Module types named only by their parent's fields.
    # Follow class-name references transitively across generated module headers.
    # The closure is concrete-model reachability, not a product-specific list.
    reachable_modules = used_modules & module_header.keys()
    changed = True
    while changed:
        changed = False
        selected_headers = {module_header[name] for name in reachable_modules}
        referenced_identifiers: set[str] = set()
        for name in selected_headers:
            referenced_identifiers.update(re.findall(r"\b[A-Za-z_]\w*\b", header_text[name]))
        additions = referenced_identifiers & module_header.keys() - reachable_modules
        if additions:
            reachable_modules.update(additions)
            changed = True

    reduced_lines: list[str] = []
    for line in umbrella_lines:
        match = INCLUDE_RE.match(line)
        if match is None:
            reduced_lines.append(line)
            continue
        name = match.group(1)
        modules = header_modules.get(name, set())
        # Keep package/type headers and retain only instantiated Module classes.
        # The original umbrella order still supplies declaration dependencies.
        # This shrinks Clang's AST without changing the concrete model hierarchy.
        if not modules or modules & reachable_modules:
            reduced_lines.append(line)

    reduced_generated = output / "cpphdl_comb_optimized_generated.h"
    reduced_externs = output / "cpphdl_comb_optimized_externs.h"
    collection_context = output / "cpphdl_comb_optimization_context.h"
    collection_manifest = output / "cpphdl_comb_collection_sources.txt"
    seed = output / "run_cpphdl_testharness_comb_optimize_seed.cpp"

    write_if_changed(reduced_generated, "\n".join(reduced_lines) + "\n")
    context_lines: list[str] = []
    for line in extern_text.splitlines():
        if line.startswith("using cpphdl_opt_t"):
            break
        if line == "#pragma once":
            continue
        if line == '#include "all_generated.h"':
            line = '#include "cpphdl_comb_optimized_generated.h"'
        context_lines.append(line)
    context_text = "#pragma once\n" + "\n".join(context_lines).strip() + "\n"
    write_if_changed(collection_context, context_text)

    alias_lines = {
        name: f"using {name} = {rhs};" for name, rhs in aliases
    }
    alias_dependencies = {
        name: set(ALIAS_NAME_RE.findall(rhs)) for name, rhs in aliases
    }

    def ordered_alias_closure(targets: list[str]) -> list[str]:
        ordered: list[str] = []
        visited: set[str] = set()

        def visit(name: str) -> None:
            if name in visited:
                return
            visited.add(name)
            for dependency in sorted(
                alias_dependencies.get(name, set()),
                key=lambda item: int(item.removeprefix("cpphdl_opt_t")),
            ):
                visit(dependency)
            ordered.append(name)

        for target in targets:
            visit(target)
        return ordered

    write_if_changed(
        reduced_externs,
        '#pragma once\n\n#include "cpphdl_comb_optimization_context.h"\n\n'
        + "\n".join(
            alias_lines[name]
            for name in ordered_alias_closure(["cpphdl_opt_t0"])
        )
        + "\n\nvoid cpphdl_optimized_root_work(cpphdl_opt_t0&, bool);\n"
          "void cpphdl_optimized_root_strobe(cpphdl_opt_t0&);\n"
          "void cpphdl_optimized_root_assign(cpphdl_opt_t0&);\n",
    )

    alias_names = [name for name, _ in aliases]
    alias_modules = {
        name: match.group(1) if (match := re.match(r"([A-Za-z_]\w*)", rhs)) else ""
        for name, rhs in aliases
    }
    alias_weights = {
        name: len(header_text.get(module_header.get(alias_modules[name], ""), "")) or 1
        for name in alias_names
    }
    opaque_names = {
        name
        for name in alias_names
        if alias_weights[name] >= args.collection_isolate_definition_bytes
    }
    # Keep the concrete root visible so L1 can flatten its lifecycle. Large
    # descendants remain ordinary opaque CppHDL subtrees.
    opaque_names.discard("cpphdl_opt_t0")
    collection_names = [name for name in alias_names if name != "cpphdl_opt_t0"]
    groups: list[list[str]] = []
    if opaque_names:
        selected_opaque = opaque_names.intersection(collection_names)
        if selected_opaque:
            groups.append(sorted(
                selected_opaque,
                key=lambda item: int(item.removeprefix("cpphdl_opt_t")),
            ))
    group: list[str] = []
    group_weight = 0
    for name in collection_names:
        if name in opaque_names:
            continue
        weight = alias_weights[name]
        if group and (
            len(group) >= args.collection_chunk_size
            or group_weight + weight > args.collection_max_definition_bytes
        ):
            groups.append(group)
            group = []
            group_weight = 0
        group.append(name)
        group_weight += weight
    if group:
        groups.append(group)
    collection_sources: list[str] = []
    for index, targets in enumerate(groups):
        source_name = f"cpphdl_comb_collect_{index:03d}.cpp"
        collection_sources.append(source_name)
        declarations = "\n".join(
            alias_lines[name] for name in ordered_alias_closure(targets)
        )
        markers = "\n".join(
            (
                f"{name}* cpphdlCombsOpaque{index:03d}_{marker:03d} = nullptr;"
                if name in opaque_names
                else f"{name}* cpphdlCombsCollect{index:03d}_{marker:03d} = nullptr;"
            )
            for marker, name in enumerate(targets)
        )
        write_if_changed(
            output / source_name,
            '#include "cpphdl_comb_optimization_context.h"\n\n'
            + declarations + "\n\n" + markers + "\n",
        )
    expected_sources = set(collection_sources)
    for stale in output.glob("cpphdl_comb_collect_*.cpp"):
        if stale.name not in expected_sources:
            stale.unlink()
    write_if_changed(
        collection_manifest,
        "\n".join(collection_sources + [seed.name]) + "\n",
    )
    write_if_changed(
        seed,
        '#include "cpphdl_comb_optimized_externs.h"\n\n'
        "static_assert(sizeof(cpphdl_opt_t0) != 0);\n"
        "cpphdl_opt_t0* cpphdlCombsCollectRoot = nullptr;\n",
    )

    print(
        f"prepared comb optimizer input: {len(reduced_lines)} umbrella lines, "
        f"{len(reachable_modules)} reachable module classes, {len(aliases)} concrete aliases, "
        f"{len(collection_sources)} bounded collection units"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
