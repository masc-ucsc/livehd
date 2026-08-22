#!/usr/bin/env python3
"""Build a gold-shaped wrapper for a cgen top with split aggregate ports."""

# PEP 604 (`X | None`) is a 3.10 feature, but this helper runs under whatever
# `python3` lgcheck finds on PATH -- inside a bazel sandbox that is macOS's
# /usr/bin/python3 (3.9), where the annotations below raise TypeError at
# class-definition time. Deferring every annotation to a string keeps the
# modern spelling and runs on 3.9.
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys


@dataclasses.dataclass(frozen=True)
class Port:
    name: str
    direction: str
    width: int
    port_id: int
    source_position: tuple[str, int, int] | None
    declaration_index: int


def parse_ports(path: pathlib.Path, top: str) -> list[Port]:
    module_line = f"module \\{top}"
    in_module = False
    ports: list[Port] = []
    source_position: tuple[str, int, int] | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not in_module:
            if line == module_line:
                in_module = True
            continue
        if line == "end":
            break
        if line.startswith("attribute \\src "):
            match = re.match(r'attribute \\src "([^"|]+):(\d+)\.(\d+)', line)
            source_position = (match.group(1), int(match.group(2)), int(match.group(3))) if match else None
            continue
        if not line.startswith("wire "):
            continue

        words = line.split()
        direction_index = next((i for i, word in enumerate(words) if word in ("input", "output")), None)
        if direction_index is None or direction_index + 2 >= len(words):
            continue
        try:
            port_id = int(words[direction_index + 1])
            width = int(words[words.index("width") + 1]) if "width" in words else 1
        except (ValueError, IndexError) as error:
            raise ValueError(f"malformed RTLIL port declaration: {raw_line}") from error
        rtlil_name = words[-1]
        if not rtlil_name.startswith("\\"):
            raise ValueError(f"unsupported non-escaped RTLIL port name: {raw_line}")
        ports.append(
            Port(
                rtlil_name[1:],
                words[direction_index],
                width,
                port_id,
                source_position,
                len(ports),
            )
        )
        source_position = None

    if not in_module:
        raise ValueError(f"top module {top!r} not found in {path}")
    if not ports:
        raise ValueError(f"top module {top!r} has no ports in {path}")
    # Keep RTLIL declaration order here.  build_adapter uses source locations
    # to recover aggregate-field order after a Yosys RTLIL round trip has
    # alphabetized the declarations.
    return ports


def order_members(members: list[Port]) -> list[Port]:
    """Return split leaves in their generated source's MSB-to-LSB order."""
    positions = [port.source_position for port in members]
    if all(position is not None for position in positions):
        source_files = {position[0] for position in positions if position is not None}
        if len(source_files) == 1:
            def source_key(port: Port) -> tuple[int, int, int]:
                assert port.source_position is not None
                return port.source_position[1], port.source_position[2], port.declaration_index

            return sorted(members, key=source_key)
    return sorted(members, key=lambda port: port.declaration_index)


def escaped(name: str) -> str:
    return f"\\{name} "


def width_range(width: int) -> str:
    return "" if width == 1 else f"[{width - 1}:0] "


def build_adapter(gold: list[Port], gate: list[Port], impl_top: str, adapter_top: str) -> str:
    gate_by_name = {port.name: port for port in gate}
    used_gate: set[str] = set()
    connections: list[tuple[Port, Port, int, int]] = []

    for gold_port in gold:
        exact = gate_by_name.get(gold_port.name)
        if exact is not None:
            members = [exact]
        else:
            prefix = gold_port.name + "."
            members = [port for port in gate if port.name.startswith(prefix)]
            members = order_members(members)
        if not members:
            raise ValueError(f"gold port {gold_port.name!r} has no corresponding implementation port")
        if any(port.direction != gold_port.direction for port in members):
            raise ValueError(f"direction mismatch while mapping gold port {gold_port.name!r}")
        member_width = sum(port.width for port in members)
        if member_width != gold_port.width:
            detail = ", ".join(f"{port.name}:{port.width}" for port in members)
            raise ValueError(
                f"width mismatch for gold port {gold_port.name!r}: gold={gold_port.width}, "
                f"implementation members={member_width} ({detail})"
            )

        next_high = gold_port.width - 1
        for member in members:
            low = next_high - member.width + 1
            connections.append((member, gold_port, next_high, low))
            used_gate.add(member.name)
            next_high = low - 1

    unused = [port.name for port in gate if port.name not in used_gate]
    if unused:
        raise ValueError("implementation has unmatched top ports: " + ", ".join(unused))

    lines = [f"module {escaped(adapter_top)}("]
    for index, port in enumerate(gold):
        comma = "," if index + 1 != len(gold) else ""
        lines.append(f"  {escaped(port.name)}{comma}")
    lines.append(");")
    for port in gold:
        lines.append(f"  {port.direction} {width_range(port.width)}{escaped(port.name)};")
    lines.append(f"  {escaped(impl_top)}dut (")
    for index, (member, gold_port, high, low) in enumerate(connections):
        if gold_port.width == member.width and high == gold_port.width - 1 and low == 0:
            expression = escaped(gold_port.name)
        elif member.width == 1:
            expression = f"{escaped(gold_port.name)}[{low}]"
        else:
            expression = f"{escaped(gold_port.name)}[{high}:{low}]"
        comma = "," if index + 1 != len(connections) else ""
        lines.append(f"    .{escaped(member.name)}({expression}){comma}")
    lines.extend(["  );", "endmodule", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gold", type=pathlib.Path, required=True)
    parser.add_argument("--gate", type=pathlib.Path, required=True)
    parser.add_argument("--gold-top", required=True)
    parser.add_argument("--gate-top", required=True)
    parser.add_argument("--impl-top", required=True)
    parser.add_argument("--adapter-top", required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    try:
        gold_ports = parse_ports(args.gold, args.gold_top)
        gate_ports = parse_ports(args.gate, args.gate_top)
        adapter = build_adapter(gold_ports, gate_ports, args.impl_top, args.adapter_top)
        args.output.write_text(adapter, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"rtlil_split_port_adapter: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
