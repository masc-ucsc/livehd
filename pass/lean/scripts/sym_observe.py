#!/usr/bin/env python3
"""Add observer outputs to one Verilog module (M3 S5: monitors from Verilog).

    sym_observe.py <in.v> <module> <out.v> name:width=expr ... [--decl 'verilog;'] ...

Each `name:width=expr` adds the output port `name` of `width` bits to `module`
and drives it with `assign name = expr;`, so an internal signal -- a stutter
witness, a piece of architectural state, a property -- becomes an output the
M3 drivers can name (`sym_verify` properties and assumptions, `sym_refine`
`pair:` / `adv:`). `--decl` text is inserted verbatim before `endmodule`, for
sequential observers (a `reg` and its `always` block) that a port then reads.

The source file is not modified; the patched copy goes through the same flow
as the original (yosys-slang -> LGraph -> single_edge -> DCERT2). Observers
only ADD outputs: the design's next-state function is unchanged, so a
property of the patched certificate's state is a property of the original's.
"""
import re
import sys


def skip_trivia(s: str, i: int) -> int:
    """Skip whitespace and comments starting at i."""
    while i < len(s):
        if s[i].isspace():
            i += 1
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = len(s) if j < 0 else j + 1
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            if j < 0:
                raise SystemExit("unterminated block comment")
            i = j + 2
        else:
            break
    return i


def match_paren(s: str, i: int) -> int:
    """Index of the ')' matching the '(' at i, skipping comments and strings."""
    assert s[i] == "("
    depth = 0
    while i < len(s):
        if s.startswith("//", i) or s.startswith("/*", i):
            i = skip_trivia(s, i)
            continue
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            i = j + 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise SystemExit("unbalanced parentheses")


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    src, module, dst = argv[1], argv[2], argv[3]
    ports, decls = [], []
    rest = argv[4:]
    k = 0
    while k < len(rest):
        if rest[k] == "--decl":
            decls.append(rest[k + 1])
            k += 2
            continue
        m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*):(\d+)=(.+)", rest[k], re.S)
        if not m:
            raise SystemExit(f"expected name:width=expr, got {rest[k]!r}")
        ports.append((m.group(1), int(m.group(2)), m.group(3)))
        k += 1
    text = open(src).read()
    hit = re.search(rf"\bmodule\s+{re.escape(module)}\b", text)
    if not hit:
        raise SystemExit(f"no module {module} in {src}")
    i = skip_trivia(text, hit.end())
    if text[i] == "#":  # parameter list
        i = skip_trivia(text, i + 1)
        i = skip_trivia(text, match_paren(text, i) + 1)
    if text[i] != "(":
        raise SystemExit(f"module {module}: no ANSI port list")
    close = match_paren(text, i)
    end = re.compile(r"\bendmodule\b").search(text, close)
    if not end:
        raise SystemExit(f"module {module}: no endmodule")
    nonempty = text[i + 1:close].strip() != ""
    port_txt = "".join(
        f"{',' if nonempty or n else ''}\n\toutput [{w - 1}:0] {name} /* sym_observe */"
        for n, (name, w, _) in enumerate(ports))
    body_txt = "".join(f"\tassign {name} = {expr};  // sym_observe\n" for name, _, expr in ports)
    body_txt += "".join(f"\t{d}  // sym_observe\n" for d in decls)
    out = (text[:close] + port_txt + "\n" + text[close:end.start()]
           + "\t// ---- observers added by sym_observe.py ----\n" + body_txt + text[end.start():])
    with open(dst, "w") as f:
        f.write(out)
    print(f"{dst}: {module} + {len(ports)} observer output(s), {len(decls)} declaration(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
