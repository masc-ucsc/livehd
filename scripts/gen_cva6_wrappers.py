#!/usr/bin/env python3
"""Generate CVA6 gate wrappers mechanically, from slang's elaborated AST.

## What a "gate wrapper" is, and why one is needed

A CVA6 leaf module usually cannot be elaborated standalone.  `ras`, for example,
takes `parameter type ras_t` — and `ras_t` is a `localparam type` declared inside
`frontend.sv`, not in any package — plus `parameter CVA6Cfg` (a giant config
struct) and `DEPTH` (derived from it).  Its `data_o` port is that struct type,
which yosys/pass.lean want as a flat vector.

A wrapper therefore has to: bind the config, bind every `type` parameter to a
concrete type, re-declare types that live in no package, flatten struct ports to
vectors, and instantiate the DUT.  There are twelve of these under
`scripts/cva6_module_wrappers/`, hand-written, 728 lines — and 68 of the 78
target modules have none.

## Why they do not need to be hand-written

`slang` already elaborates the whole core (`--top cva6`: 0 errors, 0 warnings)
and resolves every one of those facts.  For `i_ras` it reports:

    Parameter      CVA6Cfg = 17217'h8000000080000000700000005280...   (concrete)
    TypeParameter  ras_t   = frontend.ras_t
    Parameter      DEPTH   = 32'd2                                     (from CVA6Cfg.RASDepth)
    In  data_i : logic[63:0]
    Out data_o : ras.ras_t

and with `--ast-json-detailed-types` it also emits each packed struct's field
list with `bitOffset`, so the typedef can be EMITTED rather than transcribed.

So every input to a wrapper is already computed; this script is the transcription.

Usage:
  slang -f <filelist> --top cva6 --ast-json-detailed-types --ast-json ast.json
  gen_cva6_wrappers.py ast.json --out scripts/cva6_module_wrappers_gen [module ...]
"""

import argparse
import json
import os
import sys

CFG_PARAM = ("config_pkg::cva6_cfg_t CVA6Cfg = "
             "build_config_pkg::build_config(cva6_config_pkg::cva6_cfg)")


def defn_name(inst):
    """slang prints a definition as '<addr> <name>'."""
    b = inst.get("body")
    if not isinstance(b, dict):
        return None
    d = str(b.get("definition", ""))
    return d.rsplit(" ", 1)[-1] if d else None


def find_instances(root):
    out = {}
    def walk(n):
        if isinstance(n, list):
            for c in n:
                walk(c)
            return
        if not isinstance(n, dict):
            return
        if n.get("kind") == "Instance":
            d = defn_name(n)
            if d and d not in out:
                out[d] = n
        for v in n.values():
            if isinstance(v, (dict, list)):
                walk(v)
    walk(root)
    return out


# ---------------------------------------------------------------------------
# Named-type resolution.
#
# slang emits a resolved type EITHER as an expanded dict OR as the reference
# string "<addr> <scope>.<Name>", and which one you get depends on where the type
# is mentioned.  `ras_t`, for instance, appears twice: once on the `frontend`
# TypeParameter with the full PackedStructType expanded, and once on the `ras`
# port TypeParameter as "3266396442808 frontend.ras_t".  The two nodes carry
# DIFFERENT addrs, so the reference cannot be chased by address -- but the name
# is unambiguous in practice, so the expansion is indexed by bare name.
# ---------------------------------------------------------------------------

TYPE_INDEX = {}
PACKAGES = set()


def build_type_index(root):
    """name -> expanded type dict, for every named type slang did expand."""
    def walk(n):
        if isinstance(n, list):
            for c in n:
                walk(c)
            return
        if not isinstance(n, dict):
            return
        if n.get("kind") == "Package" and n.get("name"):
            PACKAGES.add(n["name"])
        nm, t = n.get("name"), n.get("type")
        if nm and isinstance(t, dict) and t.get("kind") in (
                "PackedStructType", "PackedArrayType", "EnumType"):
            TYPE_INDEX.setdefault(nm, t)
        # A TypeAlias carries its expansion under `target`, NOT `type` -- and for
        # the hpdcache family that is the ONLY place the structure appears.  Every
        # other mention of e.g. `hpdcache_req_sid_t` is a cross-scope reference
        # string (`cva6_hpdcache_wrapper.hpdcache_req_sid_t`), so missing `target`
        # meant the type was unresolvable anywhere and 18 modules fell back to a
        # bare name that no import could declare.
        if nm and n.get("kind") == "TypeAlias" and isinstance(n.get("target"), dict):
            TYPE_INDEX.setdefault(nm, n["target"])
        for v in n.values():
            if isinstance(v, (dict, list)):
                walk(v)
    walk(root)
    return TYPE_INDEX


def packed_array_of_named(t):
    """`<elemName> <range>` for a packed array whose element cannot be expanded.

    slang gives such a port a PackedArrayType DICT whose `elementType` is a
    reference STRING it never expanded -- e.g. `scoreboard_entry_t [1:0]`.  The
    width is therefore unknown, and `bare_type_name` declines because the port
    type is a dict rather than a string, so the port was reported as
    "unpacked/unknown" and the whole module skipped.  17 of the 78 CVA6 targets
    were lost this way, all to this one shape.

    But the RTL itself writes exactly `scoreboard_entry_t [N-1:0] port`
    (core/scoreboard.sv:43), and the wrapper imports the packages, so the
    declaration can simply be reproduced.
    """
    if not isinstance(t, dict) or t.get("kind") != "PackedArrayType":
        return None
    rng = t.get("range", "")
    elem = t.get("elementType")
    if isinstance(elem, dict):
        # An expanded element has a computable width; the normal path handles it.
        return None
    en = bare_type_name(elem)
    if not en or not rng:
        return None
    return f"{en} {rng}"


def bare_type_name(t):
    """Last resort: use the type's own NAME and let the imports resolve it.

    slang reports many resolved types only as "<addr> <scope>.<Name>", and does
    not emit most package typedefs as nodes this dump can index -- `fu_data_t`,
    `exception_t` and `scoreboard_entry_t` all come back NOT FOUND when searched
    for as package members.  Reconstructing them is therefore not possible from
    the AST alone.

    But it is not necessary either: the wrapper already does `import
    ariane_pkg::*` (and friends), so the bare name is visible.  If it is not, slang
    fails loudly on the generated file, which is a good failure -- the fix is one
    more import, not a silent mis-typed port.
    """
    if not isinstance(t, str):
        return None
    name = t.rsplit(" ", 1)[-1].rsplit(".", 1)[-1]
    return name if name and name != "logic" else None


def pkg_qualified(t):
    """If a reference names a type declared in a PACKAGE, return `pkg::Name`.

    A wrapper already does `import ariane_pkg::*`, so a package type can be used
    VERBATIM -- no reconstruction, no width arithmetic, and no risk of getting a
    field order wrong.  Only types declared in a MODULE scope (`frontend.ras_t`)
    have to be rebuilt from the AST.  This is what takes the generator from ~40%
    of CVA6 to most of it.
    """
    if not isinstance(t, str):
        return None
    q = t.rsplit(" ", 1)[-1]
    if "." not in q:
        return None
    scope, name = q.rsplit(".", 1)
    return f"{scope}::{name}" if scope in PACKAGES else None


def resolve_type(t):
    """Turn a reference string into the expanded type dict when possible."""
    if isinstance(t, dict):
        return t
    if isinstance(t, str):
        base = t.rsplit(" ", 1)[-1].rsplit(".", 1)[-1]
        if base in TYPE_INDEX:
            return TYPE_INDEX[base]
        if base == "logic":
            return {"kind": "ScalarType", "name": "logic"}
    return None


def type_width(t):
    """Bit width of a resolved type, or None if it is not packed/known.

    Resolves reference strings on the way in: nested struct FIELDS carry the same
    "<addr> <scope>.<Name>" form as ports and type parameters do.
    """
    t = resolve_type(t)
    if not isinstance(t, dict):
        return None
    k = t.get("kind")
    if k == "ScalarType":
        return 1
    if k == "PackedArrayType":
        r = t.get("range", "")
        try:
            hi, lo = r.strip("[]").split(":")
            n = abs(int(hi) - int(lo)) + 1
        except Exception:
            return None
        ew = type_width(t.get("elementType"))
        return None if ew is None else n * ew
    if k == "PackedStructType":
        w = 0
        for m in t.get("members", []):
            fw = type_width(m.get("type"))
            if fw is None:
                return None
            w += fw
        return w
    if k in ("EnumType", "TypeAlias"):
        return type_width(t.get("baseType") or t.get("canonicalType"))
    return None


def render_field_type(t):
    """SystemVerilog text for a field's type (packed only).

    NESTED STRUCTS ARE EMITTED INLINE, not flattened.  Collapsing a nested struct
    to `logic [w-1:0]` is bit-accurate but loses the field NAMES, and the DUT then
    fails on `instruction_o.ex.valid` with "invalid member access for type
    'logic[202:0]'" -- 13 of the remaining CVA6 failures were this one thing,
    `scoreboard_entry_t.ex` (an `exception_t`) rendered as a 203-bit vector.

    Recursing keeps the wrapper self-contained: no dependency on whether the
    nested type happens to have an importable name.
    """
    raw = t
    t = resolve_type(t)
    if not isinstance(t, dict):
        # Unexpanded named type -- use the NAME.  slang reports these already
        # package-qualified (`ariane_pkg::fu_op`), and the previous fallback to
        # `logic` is what produced "no implicit conversion from 'logic' to
        # 'fu_op'": an ENUM field silently became a bare bit, so the DUT could
        # not pass it to a function expecting the enum.
        bn = bare_type_name(raw)
        return bn if bn else "logic"
    k = t.get("kind")
    if k == "EnumType":
        # An enum must keep its name for the same reason.
        bn = bare_type_name(raw)
        if bn:
            return bn
    if k == "ScalarType":
        return "logic"
    if k == "PackedArrayType":
        inner = render_field_type(t.get("elementType", {}))
        return f"{inner} {t.get('range','')}"
    if k == "PackedStructType":
        fields = sorted(t.get("members", []), key=lambda m: m.get("bitOffset", 0), reverse=True)
        body = " ".join(f"{render_field_type(f.get('type', {}))} {f.get('name')};" for f in fields)
        return "struct packed { " + body + " }"
    w = type_width(t)
    return f"logic [{w-1}:0]" if w else "logic"


def render_struct(t, name):
    """Emit `typedef struct packed { ... } name;` from a resolved struct."""
    t = resolve_type(t)
    fields = sorted(t.get("members", []),
                    key=lambda m: m.get("bitOffset", 0), reverse=True)
    lines = ["  typedef struct packed {"]
    for f in fields:
        lines.append(f"    {render_field_type(f.get('type', {}))} {f.get('name')};")
    lines.append(f"  }} {name};")
    return "\n".join(lines)


def gen_wrapper(defn, inst):
    body = inst.get("body", {})
    mems = [m for m in body.get("members", []) if isinstance(m, dict)]
    val_params, type_params, ports = [], [], []
    has_cfg = any(isinstance(m, dict) and m.get("kind") == "Parameter"
                  and m.get("name") == "CVA6Cfg" for m in mems)
    for m in mems:
        k = m.get("kind")
        if k == "Parameter" and m.get("name") != "CVA6Cfg" and not m.get("isLocal", False):
            val_params.append(m)
        elif k == "TypeParameter" and not m.get("isLocal", False):
            type_params.append(m)
        elif k == "Port":
            ports.append(m)

    typedefs, tp_binds, unresolved = [], [], []
    pkg_types = []          # (name, text) emitted into <module>_gate_types
    recon = {}              # bare type name -> reconstructed package-visible name
    # bare type name -> the local typedef this wrapper emits for it.  A port and a
    # type parameter that name the SAME type must use the SAME local name, or the
    # port refers to an identifier the wrapper never declares.
    local_name = {}
    for tp in type_params:
        pq = pkg_qualified(tp.get("type"))
        if pq:
            tp_binds.append((tp["name"], pq))
            continue
        t   = resolve_type(tp.get("type"))
        bn0 = bare_type_name(tp.get("type"))
        nm  = f"{tp['name']}__r"

        # RECONSTRUCT whenever the type can be expanded, and bind the parameter to
        # the reconstruction.
        #
        # An earlier revision preferred the bare NAME here, to stop a flat
        # `logic [w-1:0]` typedef dropping a struct's fields.  That was wrong for
        # the commonest case: most of these names are the module's OWN
        # `parameter type` (e.g. `hpdcache_req_sid_t` is declared as
        # `parameter type hpdcache_req_sid_t = logic` in hpdcache_ctrl.sv:44), so
        # NO import can make the name visible and the wrapper failed with "use of
        # undeclared identifier" -- 18 modules.
        #
        # Reconstruction is now safe for the field-access concern too, because
        # `render_struct`/`render_field_type` recurse into nested structs and keep
        # enum names, so the rebuilt type carries its fields.
        if isinstance(t, dict):
            if t.get("kind") == "PackedStructType":
                pkg_types.append(render_struct(t, nm))
            else:
                w = type_width(t)
                if w:
                    pkg_types.append(f"  typedef {render_field_type(t)} {nm};"
                                     if render_field_type(t) != "logic" or w == 1
                                     else f"  typedef logic [{w-1}:0] {nm};")
                else:
                    if bn0:
                        tp_binds.append((tp["name"], bn0))
                        continue
                    unresolved.append(tp["name"])
                    continue
            tp_binds.append((tp["name"], nm))
            local_name[tp["name"]] = nm
            if bn0:
                # Ports naming the same type must use the SAME reconstruction, or
                # they refer to an identifier the wrapper never declares.
                recon[bn0] = nm
            continue

        # Not expandable: the name is all we have.
        if bn0:
            tp_binds.append((tp["name"], bn0))
        else:
            unresolved.append(tp["name"])

    decls, conns, glue = [], [], []
    for p in ports:
        raw, nm = p.get("type"), p.get("name")
        d = "input " if p.get("direction") == "In" else "output"
        pq = pkg_qualified(raw)
        if pq:
            # Package type: use it verbatim and connect straight through.
            decls.append(f"  {d} {pq} {nm}")
            conns.append(f"      .{nm}({nm})")
            continue
        t = resolve_type(raw)
        w = type_width(t) if isinstance(t, dict) else None
        if w is None:
            bn = bare_type_name(raw)
            if bn:
                decls.append(f"  {d} {recon.get(bn, bn)} {nm}")
                conns.append(f"      .{nm}({nm})")
                continue
            pa = packed_array_of_named(t)
            if pa:
                decls.append(f"  {d} {pa} {nm}")
                conns.append(f"      .{nm}({nm})")
                continue
            return None, f"port `{nm}` has an unpacked/unknown type"
        vec = "logic" if w == 1 else f"logic [{w-1}:0]"
        decls.append(f"  {d} {vec} {nm}")
        if isinstance(t, dict) and t.get("kind") == "PackedStructType":
            # Flatten the struct at the boundary; a packed struct IS a vector.
            sname = f"{nm}_s"
            tname = f"{nm}_t"
            typedefs.append(render_struct(t, tname))
            glue.append(f"  {tname} {sname};")
            glue.append(f"  assign {nm} = {sname};" if d.strip() == "output"
                        else f"  assign {sname} = {nm};")
            conns.append(f"      .{nm}({sname})")
        else:
            conns.append(f"      .{nm}({nm})")

    pb = [f"      .CVA6Cfg(CVA6Cfg)"] if has_cfg else []
    for tp, nm in tp_binds:
        pb.append(f"      .{tp}({nm})")
    for vp in val_params:
        pb.append(f"      .{vp['name']}({vp.get('value')})")

    pkg_block = ""
    if pkg_types:
        pkg_block = ("package " + defn + "_gate_types;\n"
                     + "\n".join(pkg_types) + "\nendpackage\n")

    out = [
        f"// GENERATED by scripts/gen_cva6_wrappers.py from slang's elaborated AST.",
        f"// Target: `{defn}` (instance `{inst.get('name')}`).  Do not edit by hand.",
        pkg_block,
        f"module {defn}_gate",
        "\n".join(f"  import {p}::*;" for p in sorted(PACKAGES))
        + (f"\n  import {defn}_gate_types::*;" if pkg_types else ""),
        f"#(",
        f"    parameter {CFG_PARAM}",
        f") (",
        ",\n".join(decls),
        f");",
        "",
        "\n".join(typedefs) if typedefs else "",
        "\n".join(glue) if glue else "",
        "",
        f"  {defn} #(",
        ",\n".join(pb),
        f"  ) dut (",
        ",\n".join(conns),
        f"  );",
        "",
        f"endmodule",
    ]
    return "\n".join(x for x in out if x is not None), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ast")
    ap.add_argument("--out", default="scripts/cva6_module_wrappers_gen")
    ap.add_argument("modules", nargs="*")
    a = ap.parse_args()

    d = json.load(open(a.ast))
    root = d.get("design", d)
    build_type_index(root)
    print(f"indexed {len(TYPE_INDEX)} named types")
    insts = find_instances(root)
    targets = a.modules or sorted(insts)
    os.makedirs(a.out, exist_ok=True)

    ok = skipped = 0
    for m in targets:
        if m not in insts:
            print(f"  SKIP {m:<34} no elaborated instance")
            skipped += 1
            continue
        text, err = gen_wrapper(m, insts[m])
        if err:
            print(f"  SKIP {m:<34} {err}")
            skipped += 1
            continue
        p = os.path.join(a.out, f"{m}_gate.sv")
        open(p, "w").write(text + "\n")
        print(f"  GEN  {m:<34} -> {p}")
        ok += 1
    print(f"\n{ok} generated, {skipped} skipped, out of {len(targets)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
