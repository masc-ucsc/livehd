#!/usr/bin/env python3
"""Convert an emitted `<mod>_Lgraph.lean` certificate into the DCERT1 wire format.

The reference converter is `Compiler.CertIO.writeCert`, which serialises a
DesignCert that Lean has already elaborated -- guaranteed faithful, but it costs
the elaboration this whole feature exists to avoid, and for the largest designs
that is hours.  This reads the literal as text instead, so a certificate can be
converted without Lean ever seeing it.

Validated by producing byte-identical output to `writeCert` on every design
where both can be run.  In production the change belongs in `pass_lean.cpp`,
which already walks these exact fields to print the literal.
"""
import re, sys

OPS = ["Op_Const","Op_Sum","Op_Sub","Op_Mult","Op_Div","Op_UDiv","Op_SDiv","Op_And",
       "Op_Or","Op_Xor","Op_Ror","Op_Not","Op_LT","Op_GT","Op_ULT","Op_UGT","Op_SLT",
       "Op_SGT","Op_EQ","Op_SHL","Op_SRA","Op_MuxBool","Op_MuxN","Op_Sext","Op_GetMask",
       "Op_SetMask","Op_MemRead","Op_MemWrite","Op_MemWriteBE"]
OPNUM = {o: i for i, o in enumerate(OPS)}
ARGOPS = {"Op_Const", "Op_Sum", "Op_MemWriteBE"}

def intlit(s):
    """`((Int.ofNat 5))`, `((-Int.ofNat 5))`, `(5)`, `(-5)`, `5` -> int."""
    s = s.strip().strip("()").strip()
    neg = s.startswith("-")
    s = s.lstrip("-").strip()
    if s.startswith("Int.ofNat"):
        s = s[len("Int.ofNat"):].strip()
    s = s.strip("()").strip()
    return -int(s) if neg else int(s)

def convert(path, out):
    txt = open(path).read()
    # sections, in emission order
    def section(name, nxt):
        i = txt.index(f"{name}", txt.index("designCert"))
        j = txt.index(nxt, i) if nxt else len(txt)
        return txt[i:j]
    src_s  = section("sources  := #[", "nodes    := #[")
    nod_s  = section("nodes    := #[", "outputs  := #[")
    out_s  = section("outputs  := #[", "flops    := #[")
    flo_s  = section("flops    := #[", "memories := #[")
    mem_s  = section("memories := #[", None)

    srcs = []
    for m in re.finditer(r"SourceDesc\.(\w+)([^\n]*)", src_s):
        kind, rest = m.group(1), m.group(2).strip()
        if kind == "memConst":
            mm = re.match(r"(\d+)\s+(\d+)\s+#\[([^\]]*)\]", rest)
            aw, dw, body = int(mm.group(1)), int(mm.group(2)), mm.group(3).strip()
            vals = [intlit(v) for v in body.split(",")] if body else []
            srcs.append("5 %d %d %d%s" % (aw, dw, len(vals),
                                          "".join(" %d" % v for v in vals)))
            continue
        toks = re.findall(r"\(\(?-?[\w.]+\s*\d*\)?\)|\S+", rest)
        if kind == "input":      srcs.append("0 %d %d" % (int(toks[0]), int(toks[1])))
        elif kind == "const":    srcs.append("1 %d %d" % (int(toks[0]), intlit(toks[1])))
        elif kind == "flopQ":    srcs.append("2 %d %d" % (int(toks[0]), int(toks[1])))
        elif kind == "flopQAsync":
            srcs.append("3 %d %d %d %d %d" % (int(toks[0]), int(toks[1]), int(toks[2]),
                                              intlit(toks[3]), 1 if toks[4] == "true" else 0))
        elif kind == "memImg":   srcs.append("4 %d %d %d" % (int(toks[0]), int(toks[1]), int(toks[2])))
        else: raise SystemExit(f"unknown SourceDesc.{kind}")

    nodes = []
    for m in re.finditer(r"\{\s*op\s*:=\s*LGraphOp\.(\w+)\s*(-?\d+)?\s*,\s*width\s*:=\s*(\d+)\s*,"
                         r"\s*deps\s*:=\s*#\[([^\]]*)\]\s*,\s*origin\s*:=\s*(\d+)", nod_s):
        op, arg, w, deps, org = m.group(1), m.group(2), int(m.group(3)), m.group(4).strip(), int(m.group(5))
        a = int(arg) if (arg is not None and op in ARGOPS) else 0
        d = [int(x) for x in deps.split(",")] if deps else []
        nodes.append("%d %d %d %d%s %d" % (OPNUM[op], a, w, len(d),
                                           "".join(" %d" % x for x in d), org))

    outs = ["%d %d" % (int(a), int(b)) for a, b in
            re.findall(r"\{\s*slot\s*:=\s*(\d+)\s*,\s*width\s*:=\s*(\d+)\s*\}", out_s)]

    flops = []
    for m in re.finditer(r"\{\s*width\s*:=\s*(\d+)\s*,\s*din\s*:=\s*(\d+)\s*,\s*enable\s*:=\s*"
                         r"(none|some\s+\d+)\s*,\s*resetPin\s*:=\s*(none|some\s+\d+)\s*,\s*"
                         r"resetValue\s*:=\s*(\([^)]*\)|-?\d+)\s*,\s*resetActiveLow\s*:=\s*(true|false)", flo_s):
        w, din, en, rp, rv, al = m.groups()
        he, e = (0, 0) if en == "none" else (1, int(en.split()[1]))
        hr, r = (0, 0) if rp == "none" else (1, int(rp.split()[1]))
        flops.append("%s %s %d %d %d %d %d %d" % (w, din, he, e, hr, r, intlit(rv), 1 if al == "true" else 0))

    mems = ["%s %s %s" % t for t in
            re.findall(r"\{\s*aw\s*:=\s*(\d+)\s*,\s*dw\s*:=\s*(\d+)\s*,\s*nextImg\s*:=\s*(\d+)\s*\}", mem_s)]

    with open(out, "w") as f:
        f.write("DCERT1\n")
        f.write("%d\n" % len(srcs));  f.write("".join(s + "\n" for s in srcs))
        f.write("%d\n" % len(nodes)); f.write("".join(s + "\n" for s in nodes))
        f.write("%d\n" % len(outs));  f.write("".join(s + "\n" for s in outs))
        f.write("%d\n" % len(flops)); f.write("".join(s + "\n" for s in flops))
        f.write("%d\n" % len(mems));  f.write("".join(s + "\n" for s in mems))
    print(f"{out}: {len(srcs)} sources, {len(nodes)} nodes, {len(outs)} outputs, "
          f"{len(flops)} flops, {len(mems)} memories")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
