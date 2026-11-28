#!/usr/bin/env python3
"""Generate a runnable simulator driver for a LEGACY `<Top>_Lgraph.lean`.

The legacy route emits the design as ordinary executable Lean -- `<Top>_comb`,
`<Top>_next`, `<Top>_step` over `<Top>_in` / `<Top>_state` / `<Top>_out` records.
Those are executable, but nothing in the route ever CALLED them: the column has
only ever been checked by elaborating the generated file.  This writes the
missing `main`, so a legacy row can be run for N cycles.

It emits no semantics.  Every value printed comes back from `<Top>_step`; this
file only feeds inputs in and prints what comes out.

FIELD NAMES COME FROM THE LEAN FILE, NOT FROM THE `_io.json` SIDECAR.  The
emitter prefixes and escapes them -- RTL `vm_enabled_o` becomes record field
`out_vm_enabled_o`, and a bit-slice like `ptw_req_data_q[35:0]` becomes
`st_ptw_req_data_q_x5b_35_x3a_0_x5d_`.  Generating against the sidecar's RTL
names produces a file that does not compile, so the structures are parsed from
the generated source, which is authoritative by construction.

THE CONSTRUCTOR FIELD LIMIT.  The state record holds one field per flop, and a
Lean constructor could hold at most 255 OBJECT fields: `lean_object` stores the
count in an 8-bit header field (`m_other:8`, include/lean/lean.h), so 256 did
not fit.  A wider record still ELABORATES and its proofs stand -- the error is
raised when the compiler compiles the inductive, not by the kernel -- but code
generation fails with "constructor '<Top>_state.mk' has too many fields", and
the file then exits non-zero even though the theorems are proved and clean.

LIFTED IN LEAN 4.36.0 (lean4 PR #15516, merged 2026-10-12).  The compiler now
keeps the first 254 object fields in the constructor and chains the rest
through spill objects, so a record of any width compiles; access costs one
pointer hop per 254 fields.  Verified here: 409, 1011 and 2403 fields all build
and run on v4.36.0-rc1.  Very wide records may additionally need
`set_option maxRecDepth` and `set_option maxHeartbeats 0`.

BEWARE EXACTLY 256 ON LEAN < 4.35.0 (lean4 issue #15527): the compiler accepts
it, the 8-bit count wraps to 0, and every field leaks on free, silently.

Usage:  scripts/make_legacy_sim.py <Top>_Lgraph.lean [--cycles N] [--out DIR] [--stim]

--stim makes the driver read a `stim v1` file named on ITS command line, the
format Futamura_Projection_Scripts/cert_trace.lean reads, and print
`STEP <k> <out0> <out1> ...` (outputs as unsigned values, in `<Top>_out` order),
cert_trace's own format, so a legacy trace and an IR or TV trace compare line
for line. Input ordinal i is the i-th field of `<Top>_in`: the exporter writes
the `_io.json` inputs and the `_in` fields in the same order. Without --stim the
driver keeps its built-in counter stimulus and `cycle N out/flop` lines.

A model with two or more clock domains has a `<Top>_edges` record (one Bool per
domain, in clock-ordinal order) that `<Top>_step` takes first. Without --stim
every clock fires on every cycle. With --stim the driver takes an optional
second argument, an `edges v1` schedule -- `at <cycle> <b0> <b1> ...`, one bit
per clock ordinal, unlisted cycles fire every clock -- the file cert_trace reads
with `edges:file:<path>`.

--trace writes ONE file, <Top>_LegacyTrace.lean: the model, then a driver that takes
cert_trace's arguments and checkpoints (pass/lean/tools/HDLProof/Run/Trace.lean, traceRun):

    <exe> <cert.dcert> <cycles> <stim> [edges:all|edges:third|edges:file:<path>]
          [init:reset|init:zero] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>]

The certificate is the one the model was emitted with: it gives the input widths cert_trace
uses, the design hash and the ordinals of `lhdcert-ckpt v1` checkpoints. The model keeps its own
state record; a checkpoint converts it by certificate ordinal (each flop `st_<name>` and memory
`st_<name>` of the certificate's `_io.json`, --io, default `<Top>_io.json` beside the model).
A name the record lacks, a width that differs, or a state field no certificate flop or memory
names is refused: the checkpoint would not hold the whole state. A restored memory is a table
of its words below its depth, built once, and its default past them. A model with one clock
domain has no quiet cycle: a schedule in which its clock does not fire is refused. Its memories
are closures, one layer per write, so the driver compacts as cert_trace does. Build it with
build-opt.sh and EXTRA_MODULES=Translation/LegacyFast (--build ROOT does that).
"""
import argparse, json, os, pathlib, re, subprocess, sys


def bool_fields(text: str, struct: str) -> list[str]:
    """Field names of `structure <struct> where` whose type is Bool, in order."""
    m = re.search(r"^structure %s where$" % re.escape(struct), text, re.M)
    if not m:
        return []
    out = []
    for line in text[m.end():].split("\n")[1:]:
        if line.startswith("deriving") or (line and not line.startswith("  ")):
            break
        f = re.match(r"\s+(\w+)\s*:\s*Bool\s*$", line)
        if f:
            out.append(f.group(1))
    return out


def fields(text: str, struct: str) -> list[tuple[str, int]]:
    """[(field name, width)] for `structure <struct> where`, in declared order."""
    m = re.search(r"^structure %s where$" % re.escape(struct), text, re.M)
    if not m:
        return []
    out = []
    for line in text[m.end():].split("\n")[1:]:
        if line.startswith("deriving") or (line and not line.startswith("  ")):
            break
        f = re.match(r"\s+(\w+)\s*:\s*BitVec\s+(\d+)\s*$", line)
        if f:
            out.append((f.group(1), int(f.group(2))))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("lean_file")
    ap.add_argument("--cycles", type=int, default=8)
    ap.add_argument("--init", default="reset", choices=("reset", "zero"),
                    help="start state: every flop at its reset value (<Top>_initState, the "
                         "default; a Verilog power-on value is carried as the reset value) "
                         "or all zero (`default`)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--stim", action="store_true",
                    help="read a `stim v1` file named on the driver's command line and print "
                         "cert_trace's `STEP k <outputs>` lines")
    ap.add_argument("--trace", action="store_true",
                    help="one file, the model and a driver with cert_trace's arguments and "
                         "lhdcert-ckpt v1 checkpoints (see the module docstring)")
    ap.add_argument("--io", default=None,
                    help="--trace: the certificate's _io.json (default <Top>_io.json beside the model)")
    ap.add_argument("--build", default="",
                    help="--trace: build the driver with build-opt.sh into this ROOT")
    ap.add_argument("--opt", default="-O2")
    ap.add_argument("--module-prefix", default="",
                    help="lake library the design module sits in, e.g. LegacySims; "
                         "with it the driver imports <prefix>.<Top>_Lgraph so `lake build` "
                         "can link a real executable")
    a = ap.parse_args()

    src = pathlib.Path(a.lean_file)
    text = src.read_text()
    m = re.search(r"^namespace (\w+)_Lgraph$", text, re.M)
    if not m:
        print(f"{src}: no `namespace <Top>_Lgraph` -- not a legacy emission", file=sys.stderr)
        return 2
    top = m.group(1)

    ins = fields(text, f"{top}_in")
    outs = fields(text, f"{top}_out")
    # the state record also carries memories, which are not `BitVec`; count every
    # declared field, because the constructor limit counts all of them
    st = re.search(r"^structure %s_state where$" % re.escape(top), text, re.M)
    nstate = 0
    if st:
        for line in text[st.end():].split("\n")[1:]:
            if line.startswith("deriving") or (line and not line.startswith("  ")):
                break
            if re.match(r"\s+\w+\s*:", line):
                nstate += 1
    if not ins or not outs:
        print(f"{src}: could not parse {top}_in/{top}_out", file=sys.stderr)
        return 2
    if a.trace:
        return trace_driver(a, src, text, top, ins, outs, st is not None)

    dst = pathlib.Path(a.out) if a.out else src.parent
    dst.mkdir(parents=True, exist_ok=True)

    sts = fields(text, f"{top}_state")          # BitVec state fields only; memories are not printable
    edges = bool_fields(text, f"{top}_edges")   # one per clock domain; empty for one domain
    every_edge = ("{ " + ", ".join(f"{e} := true" for e in edges) + " } ") if edges else ""
    in_fields = "\n".join(f"    {n} := stim cyc {i} {w}" for i, (n, w) in enumerate(ins))
    st_prints = (' ++ " " ++ '.join(f"toString (s.{n}).toNat" for n, _ in sts)) or '""' 
    out_prints = ' ++ " " ++ '.join(f"toString (o.{n}).toNat" for n, _ in outs)

    imp = (a.module_prefix + "." if a.module_prefix else "") + f"{top}_Lgraph"
    # A model emitted before <Top>_initState existed has only `default`; refuse
    # rather than silently start such a model from zero under --init reset.
    has_init = re.search(r"^def %s_initState\b" % re.escape(top), text, re.M) is not None
    # A COMBINATIONAL model has no state record and no `<Top>_step`, only
    # `<Top>_comb : <Top>_in -> <Top>_out`; it is run with a `Unit` state.
    stateless = st is None and re.search(r"^def %s_step\b" % re.escape(top), text, re.M) is None
    if a.init == "reset" and not has_init and not stateless:
        print(f"FATAL: {src} has no {top}_initState (emitted by an older pass.lean); "
              f"re-export it, or pass --init zero", file=sys.stderr)
        return 2
    state_ty = "Unit" if stateless else f"{top}_state"
    start = "()" if stateless else (f"{top}_initState" if a.init == "reset" else f"(default : {top}_state)")
    step_fn = f"(fun i (_ : Unit) => ((), {top}_comb i))" if stateless else f"{top}_step"
    lean = f'''-- GENERATED by pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py
-- Runs the legacy fast model `{top}_step` for {a.cycles} cycles.
import {imp}

-- Building an input record and printing a state record are single terms with
-- one entry per port and per flop, so both scale with the design.  The stock
-- limits are sized for ordinary source and a real design overruns them: a
-- 334-field record hits "maximum recursion depth has been reached" while
-- ELABORATING THIS DRIVER, long after the model itself compiled.  Raise both
-- unconditionally -- they cost nothing on a small design, and leaving them out
-- makes a large one fail in a place that has nothing to do with its cause.
set_option maxRecDepth 100000
set_option maxHeartbeats 0

open {top}_Lgraph

-- A deterministic stimulus: every port gets a different period, so the trace is
-- not all-zero.  Replace this one function to drive a real vector; nothing else
-- here needs to change.
def stim (cyc ord w : Nat) : BitVec w :=
  BitVec.ofNat w ((cyc + ord) % (2 ^ (min w 8)))

def mkIn (cyc : Nat) : {top}_in := {{
{in_fields}
}}

partial def run : Nat -> Nat -> {state_ty} -> IO Unit
  | 0,     _,   _ => pure ()
  | n + 1, cyc, s => do
      let (s', o) := {step_fn} {every_edge}(mkIn cyc) s
      IO.println ("cycle " ++ toString cyc ++ " out " ++ {out_prints})
      IO.println ("cycle " ++ toString cyc ++ " flop " ++ {st_prints})
      run n (cyc + 1) s'

def main : IO Unit := do
  IO.println "top {top} cycles {a.cycles} inputs {len(ins)} outputs {len(outs)} state {nstate}"
  run {a.cycles} 0 {start}
'''
    if a.stim:
        in_fields_s = "\n".join(f"    {n} := inVal st cyc {i} {w}" for i, (n, w) in enumerate(ins))
        # The edges schedule parser is always emitted (a one-domain model just
        # ignores the file); mkEdges only when the model has an edges record.
        edges_code = '''
/-- An `edges v1` schedule, read exactly as cert_trace's `edges:file:` reads it. -/
def parseEdges (txt : String) : Except String (List (Nat × Array Bool)) := do
  let body := ((txt.splitOn "\\n").map (fun l => (l.splitOn "\\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty edges file"
  | hdr :: rest =>
    if hdr != "edges v1" then .error s!"line 1: expected \\"edges v1\\", got {repr hdr}"
    rest.mapM fun l =>
      match (l.splitOn " ").filter (· != "") with
      | "at" :: c :: bits =>
          match c.toNat?, bits.mapM (fun b => match b with | "0" => some false | "1" => some true | _ => none) with
          | some c, some bs => pure (c, bs.toArray)
          | _, _ => throw s!"unparseable {repr l}"
      | _ => throw s!"unrecognised {repr l}"
'''
        if edges:
            edges_code += f"""
/-- Clock ordinal k fires unless the schedule lists this cycle with b<k> = 0. -/
def mkEdges (sched : List (Nat × Array Bool)) (cyc : Nat) : {top}_edges :=
  let bs := (sched.find? (fun r => r.1 == cyc)).map (·.2) |>.getD #[]
  {{ {", ".join(f"{e} := bs.getD {k} true" for k, e in enumerate(edges))} }}
"""
        edge_call = "(mkEdges sched cyc) " if edges else ""
        lean = f'''-- GENERATED by pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py --stim
-- Runs the legacy fast model `{top}_step` for {a.cycles} cycles on a `stim v1` file.
--   <exe> <stim file>   prints `STEP <k> <out0> <out1> ...`, cert_trace's format.
import {imp}

-- See the non-stimulus driver: both limits scale with the design's port count.
set_option maxRecDepth 100000
set_option maxHeartbeats 0

open {top}_Lgraph

/-- `stim v1`, read exactly as Futamura_Projection_Scripts/cert_trace.lean reads
it: `in <ordinal> <width> <value>` (every cycle) and
`at <cycle> in <ordinal> <width> <value>` (that cycle wins); unlisted inputs 0. -/
structure Stim where
  every : List (Nat × Nat × Int) := []
  at_   : List (Nat × Nat × Nat × Int) := []

def parseStim (txt : String) : Except String Stim := do
  let body := ((txt.splitOn "\\n").map (fun l => (l.splitOn "\\r").headD "")).filter
                (fun l => l != "" && !l.startsWith "#")
  match body with
  | [] => .error "empty stimulus file"
  | hdr :: rest =>
    if hdr != "stim v1" then .error s!"line 1: expected \\"stim v1\\", got {{repr hdr}}"
    let mut st : Stim := {{}}
    for l in rest do
      match (l.splitOn " ").filter (· != "") with
      | ["at", c, "in", i, w, v] =>
          match c.toNat?, i.toNat?, w.toNat?, v.toInt? with
          | some c, some i, some w, some v => st := {{ st with at_ := st.at_ ++ [(c, i, w, v)] }}
          | _, _, _, _ => throw s!"unparseable {{repr l}}"
      | ["in", i, w, v] =>
          match i.toNat?, w.toNat?, v.toInt? with
          | some i, some w, some v => st := {{ st with every := st.every ++ [(i, w, v)] }}
          | _, _, _ => throw s!"unparseable {{repr l}}"
      | _ => throw s!"unrecognised {{repr l}}"
    pure st

/-- The value at the stimulus's width, then placed at the field's width: the
same bits cert_trace's `mk_bv w v` hands the certificate. -/
def inVal (st : Stim) (cyc ord w : Nat) : BitVec w :=
  match st.at_.find? (fun t => t.1 == cyc && t.2.1 == ord) with
  | some (_, _, sw, v) => BitVec.ofNat w (BitVec.ofInt sw v).toNat
  | none =>
    match st.every.find? (fun t => t.1 == ord) with
    | some (_, sw, v) => BitVec.ofNat w (BitVec.ofInt sw v).toNat
    | none => 0

def mkIn (st : Stim) (cyc : Nat) : {top}_in := {{
{in_fields_s}
}}
{edges_code}
partial def run (st : Stim) (sched : List (Nat × Array Bool)) : Nat -> Nat -> {state_ty} -> IO Unit
  | 0,     _,   _ => pure ()
  | n + 1, cyc, s => do
      let (s', o) := {step_fn} {edge_call}(mkIn st cyc) s
      IO.println ("STEP " ++ toString cyc ++ " " ++ {out_prints})
      run st sched n (cyc + 1) s'

def main (args : List String) : IO UInt32 := do
  let (path, edgesPath) ← match args with
    | [p] => pure (p, none)
    | [p, q] => pure (p, some q)
    | _ => do IO.eprintln "usage: <driver> <stim v1 file> [edges v1 file]"; return 2
  let sched ← match edgesPath with
    | none => pure []
    | some q => match parseEdges (← IO.FS.readFile q) with
      | .ok rows => pure rows
      | .error e => do IO.eprintln s!"edges: {{e}}"; return 2
  match parseStim (← IO.FS.readFile path) with
  | .error e => do IO.eprintln s!"stimulus: {{e}}"; return 2
  | .ok st => do run st sched {a.cycles} 0 {start}; return 0
'''
    p = dst / f"{top}_LegacySim.lean"
    p.write_text(lean)
    print(f"wrote {p}")
    print(f"  top={top} inputs={len(ins)} outputs={len(outs)} state_fields={nstate} cycles={a.cycles}")
    if nstate == 256:
        print(f"  WARNING: exactly {nstate} state fields.  On Lean < 4.35.0 this is")
        print("           accepted but the 8-bit field count wraps to 0 and every")
        print("           field leaks on free (lean4 issue #15527).  Use >= 4.35.0.")
    elif nstate > 255:
        print(f"  NOTE: {nstate} state fields exceeds the 255 object-field limit of a")
        print("        Lean constructor.  Needs Lean >= 4.36.0, which spills the")
        print("        surplus into chained objects (lean4 PR #15516); on an older")
        print("        toolchain the proofs still stand but codegen fails.")
        print("        Very wide records may also need `set_option maxRecDepth` and")
        print("        `set_option maxHeartbeats 0`.")
    return 0


LEAN_RESERVED = {"at", "by", "do", "else", "end", "fun", "have", "if", "in", "let", "match", "namespace",
                 "open", "section", "show", "then", "theorem", "def", "where", "with", "structure",
                 "class", "instance", "deriving", "mut", "for", "return", "from", "import", "variable",
                 "universe", "axiom", "example", "abbrev", "inductive", "macro", "syntax", "local",
                 "private", "protected", "partial", "unsafe", "noncomputable", "calc", "suffices",
                 "obtain", "this", "Type", "Prop", "Sort"}


def sanitize_lean(name: str) -> str:
    """The emitter's `sanitize_lean` (pass/lean/Exporter/Frontend/lean_format.cpp)."""
    out = "".join(c if (c.isascii() and (c.isalnum() or c == "_")) else "".join(f"_x{b:02x}_" for b in c.encode())
                  for c in name)
    if not out:
        out = "id"
    if out[0].isdigit():
        out = "id_" + out
    if out in LEAN_RESERVED:
        out = "id_" + out
    return out


def mem_fields(text: str, struct: str) -> dict:
    """{field: (aw, dw)} for the memory fields `(BitVec aw -> BitVec dw)` of `<struct>`."""
    m = re.search(r"^structure %s where$" % re.escape(struct), text, re.M)
    out = {}
    if not m:
        return out
    for line in text[m.end():].split("\n")[1:]:
        if line.startswith("deriving") or (line and not line.startswith("  ")):
            break
        f = re.match(r"\s+(\w+)\s*:\s*\(BitVec\s+(\d+)\s*->\s*BitVec\s+(\d+)\)\s*$", line)
        if f:
            out[f.group(1)] = (int(f.group(2)), int(f.group(3)))
    return out


def trace_driver(a, src, text, top, ins, outs, stateful) -> int:
    io_path = pathlib.Path(a.io) if a.io else src.parent / f"{top}_io.json"
    try:
        io = json.loads(io_path.read_text())
    except (OSError, ValueError) as e:
        print(f"{io_path}: {e}", file=sys.stderr)
        return 2
    cflops = sorted(io.get("flops", []), key=lambda f: f["ordinal"])
    cmems = sorted(io.get("memories", []), key=lambda m: m["ordinal"])
    sflops = dict(fields(text, f"{top}_state"))
    smems = mem_fields(text, f"{top}_state")
    errs, flop_map, mem_map = [], [], []
    for f in cflops:
        cands = [f"st_{sanitize_lean(f['name'])}", f"st_id_{sanitize_lean(f['name'])}"]
        fld = next((c for c in cands if c in sflops), None)
        if fld is None:
            errs.append(f"flop {f['ordinal']} `{f['name']}`: no state field {cands[0]}")
        elif sflops[fld] != f["width"]:
            errs.append(f"flop {f['ordinal']} `{f['name']}`: field {fld} is {sflops[fld]} bits, the certificate's {f['width']}")
        else:
            flop_map.append((f["ordinal"], fld, f["width"]))
    for m in cmems:
        cands = [f"st_{sanitize_lean(m['name'])}", f"st_id_{sanitize_lean(m['name'])}"]
        fld = next((c for c in cands if c in smems), None)
        if fld is None:
            errs.append(f"memory {m['ordinal']} `{m['name']}`: no state field {cands[0]}")
        elif smems[fld] != (m["aw"], m["dw"]):
            errs.append(f"memory {m['ordinal']} `{m['name']}`: field {fld} is {smems[fld]}, the certificate's ({m['aw']}, {m['dw']})")
        else:
            mem_map.append((m["ordinal"], fld, m["aw"], m["dw"]))
    if [o for o, _, _ in flop_map] != list(range(len(flop_map))) or \
       [o for o, _, _, _ in mem_map] != list(range(len(mem_map))):
        errs.append("the certificate's flop or memory ordinals are not 0, 1, 2, ...")
    used = {f for _, f, _ in flop_map} | {f for _, f, _, _ in mem_map}
    for fld in list(sflops) + list(smems):
        if fld not in used:
            errs.append(f"state field {fld} holds no certificate flop or memory")
    if errs:
        print(f"{src}: --trace refused: the checkpoint would not hold the whole state", file=sys.stderr)
        for e in errs:
            print(f"  {e}", file=sys.stderr)
        return 2
    edges = bool_fields(text, f"{top}_edges")
    # the model, its imports first; OpBridge (Mathlib) becomes SemanticPrimitives when unused
    lines = text.split("\n")
    model_imports = [l for l in lines if l.startswith("import ")]
    body = "\n".join(l for l in lines if not l.startswith("import "))
    if re.search(r"\bOpBridge\.", body) is None:
        model_imports = ["import LeanSemanticPrimitives.SemanticPrimitives" if "OpBridge" in l else l
                         for l in model_imports]
        body = body.replace("\nopen OpBridge\n", "\n", 1)
    if any("OpBridge" in l or "Mathlib" in l for l in model_imports):
        print(f"{src}: --trace refused: the model uses OpBridge (Mathlib), which build-opt.sh does not build",
              file=sys.stderr)
        return 2
    imports = list(dict.fromkeys(model_imports + ["import HDLProof.Run.Trace",
                                                  "import LeanSemanticPrimitives.Compiler.CertIOCodec"]))
    state_ty = f"{top}_state" if stateful else "Unit"
    init_reset = f"{top}_initState" if stateful else "()"
    init_zero = f"(default : {top}_state)" if stateful else "()"
    flops_v = ", ".join(f"mk_bv {w} ((s.{fld}).toNat : Int)" for _, fld, w in flop_map)
    mems_v = ", ".join(f"memImage (D.memories[{k}]!) s.{fld}" for k, fld, _, _ in mem_map)
    restore_fields = ",\n    ".join(
        [f"{fld} := BitVec.ofNat {w} (c.flops[{k}]!).2" for k, fld, w in flop_map] +
        [f"{fld} := tabRead (memTab {dw} c.mems[{k}]!) (BitVec.ofNat {dw} c.mems[{k}]!.dflt)" for k, fld, _, dw in mem_map])
    restore_body = (f"{{ ({top}_initState) with\n    {restore_fields} }}" if stateful and restore_fields
                    else init_reset)
    in_v = ",\n    ".join(f"{n} := BitVec.ofNat {w} (bv_uint (i.getD {k} (mk_bv 0 0))).toNat" for k, (n, w) in enumerate(ins))
    out_v = ", ".join(f"mk_bv {w} ((o.{n}).toNat : Int)" for n, w in outs)
    if not stateful:
        step_body = f"  .ok (outs ({top}_comb (mkIn i)), s)"
    elif edges:
        ev = ", ".join(f"{e} := e.getD {k} true" for k, e in enumerate(edges))
        step_body = f"  let (s', o) := {top}_step {{ {ev} }} (mkIn i) s\n  .ok (outs o, s')"
    else:
        step_body = (f"  if e.all id then\n    let (s', o) := {top}_step (mkIn i) s\n    .ok (outs o, s')\n"
                     f"  else .error \"this legacy model has one clock domain and no quiet cycle\"")
    driver = f'''
/-! ## The trace driver (make_legacy_sim.py --trace): cert_trace's arguments and checkpoints -/

set_option maxRecDepth 100000
set_option maxHeartbeats 0

namespace {top}_LegacyTrace
open {top}_Lgraph Compiler

/-- A memory restored from a checkpoint: its words below its depth, a table built once, and the
default past them. -/
@[noinline] def tabRead {{aw dw : Nat}} (t : Array (BitVec dw)) (dflt : BitVec dw) (a : BitVec aw) :
    BitVec dw :=
  if h : a.toNat < t.size then t[a.toNat] else dflt

def memTab (dw : Nat) (m : Ckpt.MemCk) : Array (BitVec dw) := (Ckpt.memTable m).map (BitVec.ofNat dw)

/-- A model memory as a reference image: its word at every address the model has, the
certificate's power-on image at any other (where every route keeps it). -/
def memImage {{aw dw : Nat}} (md : MemoryDesc) (f : BitVec aw → BitVec dw) : Int → BV :=
  fun a => if 0 ≤ a ∧ a < 2 ^ aw then mk_bv dw ((f (BitVec.ofNat aw a.toNat)).toNat : Int) else md.powerOnImg a

/-- The state a checkpoint is taken of, by certificate ordinal. -/
def view (D : DesignCert) (s : {state_ty}) : RuntimeState :=
  {{ flops := #[{flops_v}], mems := #[{mems_v}] }}

/-- A checkpoint `Ckpt.validate` accepted, as the model's state. -/
def restore (c : Ckpt.Ckpt) : {state_ty} :=
  {restore_body}

def mkIn (i : RuntimeInput) : {top}_in := {{
    {in_v} }}

def outs (o : {top}_out) : Array BV := #[{out_v}]

def step (e : ClockEdges) (i : RuntimeInput) (s : {state_ty}) : Except String (Array BV × {state_ty}) :=
{step_body}

end {top}_LegacyTrace

open {top}_Lgraph in
def main (args : List String) : IO UInt32 := do
  match args with
  | cert :: rest =>
    let D ← match Compiler.CertIO.readCertChecked (← IO.FS.readBinFile cert) with
      | .ok D => pure D
      | .error e => do IO.eprintln s!"{{cert}}: {{e}}"; return 2
    unless D.flops.size == {len(flop_map)} && D.memories.size == {len(mem_map)} &&
        D.outputs.size == {len(outs)} do
      IO.eprintln s!"{{cert}}: not the certificate this model was emitted with: {{D.flops.size}} flops, \
{{D.memories.size}} memories, {{D.outputs.size}} outputs (the model: {len(flop_map)}, {len(mem_map)}, {len(outs)})"
      return 2
    HDLProof.Run.traceRun "legacy" D
      {{ init := fun z => if z then {init_zero} else {init_reset}
        step := {top}_LegacyTrace.step
        view := {top}_LegacyTrace.view D
        restore := {top}_LegacyTrace.restore
        -- the model's memories are closures, one layer per write: compaction replaces the
        -- state by its restored checkpoint (a table per memory), as cert_trace does
        compact := some fun hash k s =>
          {top}_LegacyTrace.restore (Compiler.Ckpt.save D hash k #[] ({top}_LegacyTrace.view D s)) }} rest
  | [] =>
    IO.eprintln "usage: <legacy trace> <cert.dcert> <cycles> <stim> [edges:all|edges:third|edges:file:<path>] \
[init:reset|init:zero] [ckpt-in:<file>] [ckpt-at:<cycle>:<file>] [ckpt-out:<file>]"
    return 2
'''
    head = ("-- GENERATED by pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py --trace\n"
            f"-- The legacy fast model {top}_Lgraph ({src.name}), then its trace driver.\n")
    dst = pathlib.Path(a.out) if a.out else src.parent
    dst.mkdir(parents=True, exist_ok=True)
    p = dst / f"{top}_LegacyTrace.lean"
    p.write_text(head + "\n".join(imports) + "\n" + body.rstrip() + "\n" + driver)
    print(f"wrote {p}")
    print(f"  top={top} flops={len(flop_map)} memories={len(mem_map)} inputs={len(ins)} outputs={len(outs)} "
          f"clock_domains={max(1, len(edges))}")
    if a.build:
        build_opt = pathlib.Path(__file__).resolve().parents[4] / "formal/lean/Futamura_Projection_Scripts/build-opt.sh"
        env = dict(os.environ, EXTRA_MODULES="Translation/LegacyFast", OPT=a.opt,
                   ROOT=str(pathlib.Path(a.build).resolve()))
        r = subprocess.run(["bash", str(build_opt), str(p.resolve())], cwd=build_opt.parent.parent, env=env)
        if r.returncode != 0:
            print("make_legacy_sim: build failed", file=sys.stderr)
            return 1
        print(f"built {pathlib.Path(a.build).resolve() / 'nat' / p.stem}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
