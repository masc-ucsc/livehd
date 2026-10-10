# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Count physical cells through the hierarchy emitted by cgen in these fixtures."""
from pathlib import Path
import re
import sys


def count_cells(directory: str, top: str, cell: str) -> int:
    modules = {}
    for path in Path(directory).glob("*.v"):
        for name, body in re.findall(r"\bmodule\s+(\\\S+|\w+)\s*(?=\(|#)(.*?)\bendmodule\b", path.read_text(), re.S):
            modules[name.lstrip("\\")] = [
                kind.lstrip("\\") for kind in re.findall(
                    r"^\s*(\\\S+|\w+)\s+(?:#\s*\([^;]*?\)\s*)?(?:\\\S+|[\w.$]+)\s*\(",
                    body, re.M,
                )
            ]
    if top not in modules:
        top = top.replace(".", "_") if top.replace(".", "_") in modules else top.split(".")[-1]
    assert top in modules, (top, list(modules))

    def visit(name, stack):
        assert name not in stack, ("recursive hierarchy", name)
        return sum(1 if child == cell else visit(child, stack | {name})
                   if child in modules else 0 for child in modules[name])

    return visit(top, set())


if __name__ == "__main__":
    print(count_cells(*sys.argv[1:]))
