#!/usr/bin/env python3
#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pubify_equiv — mark an equiv DUT's top lambda `pub` so a `_tb.prp` can import
# it. Idempotent: does nothing if the lambda is already public or cannot be
# located (e.g. an `lg=`-renamed module whose Verilog name differs from the
# source lambda). Reads the `:pyrope_top:` header to find the lambda name.

import re
import sys


def top_lambda(src):
    m = re.search(r':pyrope_top:\s*([^\s*]+)', src)
    if not m:
        return None
    top = m.group(1)
    return top.rsplit('.', 1)[-1] if '.' in top else top


def pubify(path):
    src = open(path).read()
    lam = top_lambda(src)
    if not lam:
        return False
    # a definition line: optional `pub`, then mod/comb/pipe/fun, optional
    # `[depth]` / `::[attr]` / generic `[T]`, whitespace, then the lambda name.
    pat = re.compile(
        r'^(\s*)(pub\s+)?(mod|comb|pipe|fun)((?:\s*(?:::)?\[[^\]]*\])?\s+)(' + re.escape(lam) + r')\b',
        re.M)
    changed = [False]

    def repl(m):
        if m.group(2):                    # already `pub`
            return m.group(0)
        changed[0] = True
        return m.group(1) + "pub " + m.group(3) + m.group(4) + m.group(5)

    new = pat.sub(repl, src, count=1)
    if changed[0]:
        open(path, "w").write(new)
    return changed[0]


if __name__ == "__main__":
    for p in sys.argv[1:]:
        pubify(p)
