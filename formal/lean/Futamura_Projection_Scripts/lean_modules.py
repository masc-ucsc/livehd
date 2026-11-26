#!/usr/bin/env python3
"""The local Lean modules a set of roots imports, in dependency order.

    lean_modules.py ROOT...

A ROOT is a module name (LeanSemanticPrimitives.Compiler.ResidualWork, HDLProof.Run.Sound) or a
.lean file (a driver or probe: its imports are roots, the file itself is not listed). Modules
under LeanSemanticPrimitives are found in formal/lean, modules under HDLProof in
pass/lean/tools; Init, Std and Lean are the toolchain's and are skipped.

Prints one module per line, as its path without `.lean` (LeanSemanticPrimitives/Compiler/X,
HDLProof/Run/Y), every module after the modules it imports. Exits 1, naming the importer, when
an imported local file is missing, when the imports form a cycle, or when anything imports
Mathlib (these builds are Mathlib-free).

Only a file's header contributes dependencies: comments, optional `module` and `prelude`, then imports up to the first
other command (an `import` inside a comment is text, not an import).

Used by .build-proj.sh and Futamura_Projection_Scripts/build-opt.sh, so their module lists are
the import closures of a few roots instead of lists kept in order by hand.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEAN = os.path.dirname(HERE)  # formal/lean
TOOLS = os.path.normpath(os.path.join(LEAN, '..', '..', 'pass', 'lean', 'tools'))
LOCAL = {'LeanSemanticPrimitives': LEAN, 'HDLProof': TOOLS}
TOOLCHAIN = {'Init', 'Std', 'Lean', 'Lake'}


def strip_comments_prefix(text):
    """Yield the header's words: the text before the first command other than prelude/import,
    with block comments (nested) and line comments removed."""
    out = []
    i, n, depth = 0, len(text), 0
    while i < n:
        if text.startswith('/-', i):
            if depth == 0:
                out.append(' ')  # a comment between tokens is whitespace
            depth += 1
            i += 2
        elif depth > 0 and text.startswith('-/', i):
            depth -= 1
            i += 2
        elif depth > 0:
            i += 1
        elif text.startswith('--', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def header_imports(path):
    """The modules `path` imports (its header only)."""
    with open(path, encoding='utf-8') as f:
        text = f.read()
    words = strip_comments_prefix(text).split()
    mods = []
    k = 1 if words and words[0] == 'module' else 0
    while k < len(words):
        w = words[k]
        if w == 'prelude':
            k += 1
        else:
            while k < len(words) and words[k] in ('public', 'meta'):
                k += 1
            if k + 1 >= len(words) or words[k] != 'import':
                break
            k += 1
            if words[k] == 'all' and k + 1 < len(words):
                k += 1
            mods.append(words[k])
            k += 1
    return mods


def path_of(mod):
    base = LOCAL.get(mod.split('.')[0])
    return None if base is None else os.path.join(base, *mod.split('.')) + '.lean'


def closure(roots):
    order, state = [], {}

    def visit(mod, via):
        st = state.get(mod)
        if st == 'done':
            return
        if st == 'open':
            sys.exit(f'lean_modules: import cycle through {mod} (from {via})')
        top = mod.split('.')[0]
        p = path_of(mod)
        if p is None:
            if top == 'Mathlib':
                sys.exit(f'lean_modules: {via} imports {mod}; this build is Mathlib-free')
            if top not in TOOLCHAIN:
                sys.exit(f'lean_modules: {via} imports {mod}, which is neither local nor the toolchain\'s')
            return
        if not os.path.isfile(p):
            sys.exit(f'lean_modules: {via} imports {mod}, but {p} does not exist')
        state[mod] = 'open'
        for d in header_imports(p):
            visit(d, mod)
        state[mod] = 'done'
        order.append(mod)

    for r in roots:
        if r.endswith('.lean'):
            if not os.path.isfile(r):
                sys.exit(f'lean_modules: no root file {r}')
            for d in header_imports(r):
                visit(d, r)
        else:
            visit(r, 'the root list')
    return order


def main(argv):
    if not argv:
        sys.exit(__doc__)
    for m in closure(argv):
        print(m.replace('.', '/'))


if __name__ == '__main__':
    main(sys.argv[1:])
