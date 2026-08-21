#!/usr/bin/env python3
"""Inspect one RTLIL module's direct children or input ports."""

import argparse
import pathlib
import sys


PortShape = dict[str, tuple[str, int]]


def unescape(identifier: str) -> str:
    return identifier[1:] if identifier.startswith("\\") else identifier


def collect_port_shapes(lines: list[str], wanted: set[str]) -> dict[str, PortShape]:
    """Return aggregate-aware port shapes for selected RTLIL modules."""
    shapes: dict[str, PortShape] = {}
    current = ""
    for raw_line in lines:
        if raw_line.startswith("module "):
            current = unescape(raw_line.split(maxsplit=1)[1])
            if current in wanted:
                shapes[current] = {}
            continue
        if raw_line == "end":
            current = ""
            continue
        if current not in shapes:
            continue
        line = raw_line.strip()
        if not line.startswith("wire "):
            continue
        words = line.split()
        direction = next((word for word in words if word in ("input", "output")), "")
        if not direction:
            continue
        try:
            width = int(words[words.index("width") + 1]) if "width" in words else 1
        except (ValueError, IndexError):
            continue
        name = unescape(words[-1])
        # cgen exposes a packed aggregate as escaped dotted leaves. Fold both
        # representations to the same root and total width so specialization
        # selection can distinguish e.g. an 8-bit occurrence from a 1-bit
        # default even when one side is packed and the other is split.
        root = name.split(".", maxsplit=1)[0]
        old_direction, old_width = shapes[current].get(root, (direction, 0))
        if old_direction != direction:
            # An input/output collision cannot be a compatible interface.
            shapes[current][root] = ("mixed", old_width + width)
        else:
            shapes[current][root] = (direction, old_width + width)
    return shapes


def compatible_specialization(
    child: str,
    mapped_modules: set[str],
    gate_shapes: dict[str, PortShape],
    mapped_shapes: dict[str, PortShape],
) -> str:
    """Choose the best occurrence specialization by its elaborated interface."""
    base = child.split("$", maxsplit=1)[0]
    candidates = sorted(
        name for name in mapped_modules if name == base or name.startswith(base + "$")
    )
    gate = gate_shapes.get(child, {})
    ranked: list[tuple[int, str]] = []
    for candidate in candidates:
        gold = mapped_shapes.get(candidate, {})
        common = set(gate).intersection(gold)
        if not common or any(gate[name] != gold[name] for name in common):
            continue
        # Prefer the candidate whose compatible shared interface carries the
        # most information. Equal-shape repeated generated instances are
        # behaviorally the same specialization; lexical order is deterministic.
        score = sum(gate[name][1] for name in common)
        ranked.append((score, candidate))
    if not ranked:
        # A cgen-only helper/clone has no standalone reference obligation.  Do
        # not manufacture its stripped base as a source top: that turns an
        # otherwise useful recursive descent into a setup failure when the base
        # does not exist (Minion's generated thread-buffer `_p1` occurrence).
        return "-"
    best_score = max(score for score, _ in ranked)
    return min(name for score, name in ranked if score == best_score)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rtlil", type=pathlib.Path, required=True)
    parser.add_argument("--top", required=True)
    parser.add_argument("--with-base", action="store_true")
    parser.add_argument(
        "--map-against",
        type=pathlib.Path,
        help="prefer an occurrence-specialized child name when that module exists in this RTLIL",
    )
    parser.add_argument("--has-input")
    args = parser.parse_args()

    try:
        lines = args.rtlil.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        print(f"rtlil_children: {error}", file=sys.stderr)
        return 1

    modules = {unescape(line.split(maxsplit=1)[1]) for line in lines if line.startswith("module ")}
    mapped_modules: set[str] = set()
    mapped_lines: list[str] = []
    if args.map_against is not None:
        try:
            mapped_lines = args.map_against.read_text(encoding="utf-8").splitlines()
        except OSError as error:
            print(f"rtlil_children: {error}", file=sys.stderr)
            return 1
        mapped_modules = {
            unescape(line.split(maxsplit=1)[1]) for line in mapped_lines if line.startswith("module ")
        }
    wanted = f"module \\{args.top}"
    in_module = False
    found = False
    has_input = False
    children: list[str] = []
    seen: set[str] = set()
    for raw_line in lines:
        line = raw_line.strip()
        if not in_module:
            if line == wanted:
                in_module = True
                found = True
            continue
        if raw_line == "end":
            break
        if args.has_input is not None and line.startswith("wire "):
            words = line.split()
            if "input" in words and unescape(words[-1]) == args.has_input:
                has_input = True
            continue
        if not line.startswith("cell "):
            continue
        words = line.split()
        if len(words) < 3:
            continue
        child = unescape(words[1])
        if child in modules and child not in seen:
            seen.add(child)
            children.append(child)

    if not found:
        print(f"rtlil_children: top module {args.top!r} not found in {args.rtlil}", file=sys.stderr)
        return 1
    if args.has_input is not None:
        print("yes" if has_input else "no")
        return 0
    gate_shapes: dict[str, PortShape] = {}
    mapped_shapes: dict[str, PortShape] = {}
    if args.with_base and mapped_modules:
        gate_shapes = collect_port_shapes(lines, set(children))
        child_bases = {child.split("$", maxsplit=1)[0] for child in children}
        candidates = {
            name
            for name in mapped_modules
            if any(name == base or name.startswith(base + "$") for base in child_bases)
        }
        mapped_shapes = collect_port_shapes(mapped_lines, candidates)
    for child in children:
        if args.with_base:
            base = child.split("$", maxsplit=1)[0]
            if child in mapped_modules:
                mapped = child
            elif not mapped_modules:
                mapped = base
            else:
                mapped = compatible_specialization(child, mapped_modules, gate_shapes, mapped_shapes)
            print(f"{child}\t{mapped}")
        else:
            print(child)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
