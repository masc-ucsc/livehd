# D1 — the transcription boundary: what is actually checked, what the field does, what we could do

Research note. Nothing here is implemented.

## 1. The chain, and what polices each link

```
RTL (.sv)
  │ ①  yosys-slang reader                     inou/yosys
  ▼
LGraph (raw)
  │ ②  pass.single_edge normalisation         pass/single_edge
  ▼
LGraph (normalised)
  │ ③  pass.lean exporter                     design_scan → certificate_ir →
  ▼                                           memory_lowering → emit_design_cert/emit_dcert
DesignCert
  │ ④  interpretDesign / directStep / compileDesign
  ▼
semantics
```

| link | what checks it | kind |
|---|---|---|
| ④ | `directStep_correct`, `compileDesign_correct`, `compileAndRun_correct`, `directStep_eq_compileAndRun`, the trace lifts | **proved in Lean**, 0 axioms, 0 `sorry` |
| ①+② | `lhd lec --impl lg:<norm> --ref lg:<ref>` where the reference is an INDEPENDENT Slang read of the same RTL | formal, per module, **off by default** (`RUN_LEC_GATE=false`) |
| ③ | — | **nothing formal** |

### A correction this note exists to make

The audit said the boundary "is policed only by the Verilator differential —
which is how both `txfmafrac_top` defects were found." That is wrong in a way
that matters.

`v2v_verilator_diff.py` compares the ORIGINAL RTL against the REGENERATED
Verilog (`inou/cgen`). It crosses ① ② and the cgen writer. **It never reads a
DesignCert.** It cannot see a defect in ③, because ③ is not on its path.

The only thing in the tree that crosses ③ end to end is
`mem_lean_diffsim.py` — source RTL under iverilog against the normalised Lean
certificate — and it was written for mixed-timing memories, not as a
corpus-wide gate.

The static gates do not close this either. `_comb/_next/_step_refines_fast`,
the op census and `sorry=0` check the emitted file against itself; both sides
come out of the same C++, so they establish internal consistency of an
emission, not its fidelity to the LGraph.

**So the exporter — the largest and most semantic step in the chain — is
checked corpus-wide by nothing, and spot-checked by one memory-focused
differential.**

## 2. What the field does

Four families, with the hardware instances named.

**(a) Verify the translator.** CompCert is the reference point, and even it
retains a TCB worth a paper of its own
([CompCert TCB](https://arxiv.org/pdf/2201.10280)). The hardware instances are
**Lutsig**, a verified Verilog compiler in HOL4 whose theorem is that an
accepted design and its netlist agree at every cycle modulo removed X
behaviour ([Lutsig, CPP 2021](https://www.cs.rhul.ac.uk/home/upac096/papers/cpp2021.pdf)),
**Vericert**, verified C→Verilog HLS in Coq on top of CompCert, using a
Verilog semantics ported from Lööw's HOL4 one
([Vericert, OOPSLA 2021](https://johnwickerson.github.io/papers/vericert_oopsla21.pdf)),
and the Kôika/Kami line. All are compilers *written in order to be verified*.
None is a retrofit onto an existing C++ pass, which is what ③ is.

**(b) Translation validation — validate each run, not the tool.** Pnueli,
Siegel and Singerman's idea; the systems-scale instance is seL4's binary
verification, where GCC could not be verified so each build is validated by
SMT refinement between C and the binary through GraphLang
([Sewell, Myreen, Klein, PLDI 2013](https://www.cl.cam.ac.uk/~mom22/pldi13.pdf),
[graph-refine](https://github.com/seL4proj/graph-refine)). This is the closest
STRUCTURAL analogue to ③: an untrusted translator in the middle, validated per
instance rather than once. Lutsig uses the same technique for its
technology-mapping step specifically.

**(c) Bounded translation validation — automatic, incomplete, bug-finding.**
**Alive2** validates LLVM optimisations by refinement with bounded unrolling.
It is explicit that it misses bugs, avoids false alarms, needs no compiler
change, and it found 47 new LLVM miscompiles in the paper (50+ since), with
LLVM developers running it daily
([Alive2, PLDI 2021](https://www-old.cs.utah.edu/~regehr/alive2-pldi21.pdf)).
Its stated limits — solver scalability, fixed bitwidths, bounded loops — are
exactly the limits any version of this would have here.

**(d) Certifying tools plus a verified checker.** The solver is untrusted and
emits a certificate a small trusted checker validates. We already run this
pattern inside M3: cadical → LRAT → Lean's verified checker. On the hardware
side it is live research: **Certifaiger / Cerbtora** give bit-level and
word-level certificate checkers for hardware model checking and can route SAT
results through DRAT/LRAT so the solver need not be trusted, with the authors
working toward a fully trusted pipeline in a theorem prover including verified
bit-blasting
([Certifaiger and Cerbtora](https://link.springer.com/chapter/10.1007/978-3-032-32589-1_17));
**Vera** proposes a Coq-verified Verilog equivalence checker via SMT-LIB and
SMTCoq ([Vera, LATTE 2025](https://johnwickerson.github.io/papers/vera_LATTE25.pdf));
and a 2026 paper on certified sequential equivalence after retiming concludes
that a dedicated certificate-emitting sequential equivalence checker is still
missing ([arXiv 2607.27498](https://arxiv.org/pdf/2607.27498)).

**A gap worth knowing.** A search for a verified netlist importer in Coq or
Lean found none. Option O3 below is therefore unexplored territory rather than
a matter of adopting an existing tool.

## 3. Options for ③

**O1 — per-design equivalence between the LGraph and the certificate.**
Render the DesignCert back into a comparable artifact and LEC it against the
normalised LGraph. The seL4 shape, reusing `lhd lec`. The renderer becomes new
trusted code unless it is itself proof-carrying — and a second implementation
that shares the first one's assumptions validates nothing. `Compiler/Reify`
already emits a straight-line `Foo_step` WITH a proof that it equals
`denoteResidual R`, so the rendered side can carry its own proof; that is what
makes this option credible rather than circular.

**O2 — corpus-wide bounded differential across ③ (the Alive2 posture).**
Generalise `mem_lean_diffsim.py` from memories to the whole corpus: same
stimulus into the RTL simulator and into the certificate under the Lean
simulator, compare at period boundaries. Cheap, finds real defects, proves
nothing. It closes the MEASUREMENT gap identified in §1 — today ③ is sampled
on memories only.

**O3 — move ③ into Lean and shrink the trusted step.**
C++ serialises the LGraph faithfully; Lean parses it and builds the
certificate, with the construction proved. Trusted shrinks from
scan + IR build + memory lowering + emit (large, semantic) to a serialiser
(small, mechanical). In-tree precedent: `Compiler/CertIO` is already a
verified Lean codec for DCERT2. Outside precedent: none found.

**O4 — certificate-carrying equivalence with our own verified checker.**
Miter the certificate against a yosys-derived model of the RTL, bit-blast
through Std's verified blaster, solve with untrusted cadical, check the LRAT in
Lean. Every piece already exists here (`sym_verify`, the blaster, the LRAT
checker). Yields a per-design PROVED statement with the solver untrusted,
bounded by k unless the obligation goes inductive. This is the
Certifaiger/Vera direction, and we are unusually well placed for it.

**O5 — accept and bound precisely.** Change no code; state what each link is
checked by and what the residual is. What the audit attempts today, except
with the v2v claim corrected.

## 4. Comparison

| | closes ③ | strength of result | new trusted code | effort | risk |
|---|---|---|---|---|---|
| O1 | yes, per design | proof, if the renderer is proof-carrying | renderer (mitigable via `Reify`) | medium | circularity if rendered by C++ |
| O2 | partially | evidence only, bounded | none | **low** | false comfort if sold as proof |
| O3 | yes, structurally | proof | serialiser only | **high** | research project; no prior art |
| O4 | yes, per design | proof, solver untrusted | none — all in-tree | medium | k-bounded; solver scaling |
| O5 | no | none | none | trivial | none, if stated honestly |

## 5. Recommendation

Staged, cheapest-first, each step useful alone:

1. **O5 immediately** — the v2v claim is wrong and currently provides false
   comfort about the least-checked link in the chain.
2. **O2 next** — days of work, and it turns ③ from "sampled on memories" into
   "exercised corpus-wide". Report it as bug-finding, never as proof.
3. **O4 as the real answer** — a per-design proved equivalence with the solver
   untrusted, built from machinery already trusted here. This is where the
   boundary actually starts to carry weight.
4. **O3 only if** the boundary must bear assurance weight that per-design
   validation cannot. It is the only option that shrinks the TCB rather than
   checking around it, and the literature says nobody has done it.

O1 is subsumed by O4: both validate per design, and O4's result is stronger
for the same reason the M3 route already prefers it — the solver does not have
to be believed.

## Sources

- [Lutsig: A Verified Verilog Compiler for Verified Circuit Development (CPP 2021)](https://www.cs.rhul.ac.uk/home/upac096/papers/cpp2021.pdf)
- [Formal Verification of High-Level Synthesis — Vericert (OOPSLA 2021)](https://johnwickerson.github.io/papers/vericert_oopsla21.pdf)
- [Translation Validation for a Verified OS Kernel (PLDI 2013)](https://www.cl.cam.ac.uk/~mom22/pldi13.pdf) and [graph-refine](https://github.com/seL4proj/graph-refine)
- [Alive2: Bounded Translation Validation for LLVM (PLDI 2021)](https://www-old.cs.utah.edu/~regehr/alive2-pldi21.pdf)
- [Hardware Model Checking Certification with Certifaiger and Cerbtora](https://link.springer.com/chapter/10.1007/978-3-032-32589-1_17)
- [Towards mechanized verification of Verilog equivalence checking — Vera (LATTE 2025)](https://johnwickerson.github.io/papers/vera_LATTE25.pdf)
- [Certified Sequential Sweep Without Unrolling (arXiv 2607.27498)](https://arxiv.org/pdf/2607.27498)
- [The Trusted Computing Base of the CompCert Verified Compiler](https://arxiv.org/pdf/2201.10280)
