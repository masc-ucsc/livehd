"""The sky130 HD test libraries the lhd smokes share: the seven-cell fixture
(inou/lefdef/tests/data/sky130hd.{lib,lef}) with more cells spliced in, from the
sequential fragment (sky130hd_seq.{lib,lef}.fragment: ICGs, latches, dfrtp_1)
or a smoke's own .part files. Run from the runfiles root, like the smokes."""
from pathlib import Path

DATA = Path('inou/lefdef/tests/data')
SEQ_LIB = DATA / 'sky130hd_seq.lib.fragment'
SEQ_LEF = DATA / 'sky130hd_seq.lef.fragment'


def lib_cell(text, name):
    """The `cell ("name") { ... }` group of a Liberty fragment."""
    start = text.index(f'cell ("{name}")')
    depth = 0
    for i in range(text.index('{', start), len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1] + '\n'
    raise ValueError(f'unbalanced Liberty cell {name}')


def lef_macro(text, name):
    """The `MACRO name ... END name` block of a LEF fragment."""
    start = text.index(f'MACRO {name}\n')
    end = text.index(f'END {name}\n', start)
    return text[start:end + len(f'END {name}\n')]


def splice(root, stem, lib_cells='', lef_macros=''):
    """Write root/stem.lib and root/stem.lef: the fixture plus the given cell groups. Returns both paths."""
    lib = (DATA / 'sky130hd.lib').read_text()
    end = lib.rindex('}')
    (root / f'{stem}.lib').write_text(lib[:end] + lib_cells + lib[end:])
    lef = (DATA / 'sky130hd.lef').read_text()
    end = lef.rindex('END LIBRARY')
    (root / f'{stem}.lef').write_text(lef[:end] + lef_macros + '\n' + lef[end:])
    return root / f'{stem}.lib', root / f'{stem}.lef'


def seq_library(root, stem='seq', cells=None):
    """The fixture plus the sequential fragment, or only its `cells` (by name)."""
    lib, lef = SEQ_LIB.read_text(), SEQ_LEF.read_text()
    if cells is not None:
        lib = ''.join(lib_cell(lib, name) for name in cells)
        lef = ''.join(lef_macro(lef, name) for name in cells)
    return splice(root, stem, lib, lef)


def part_library(root, stem, part):
    """The fixture plus a smoke's own lhd/tests/<part>.{lib,lef}.part cells."""
    return splice(root, stem, Path(f'lhd/tests/{part}.lib.part').read_text(), Path(f'lhd/tests/{part}.lef.part').read_text())
