# LiveHD LEC guide (`lhd lec`)

`lhd lec` is LiveHD's single logic-equivalence (LEC) command — `prove_equal(ref,
impl)`. The `--set lec.solver` knob picks the backend (the former `lhd check` is
now `--set lec.solver=lgyosys`):

| `--set lec.solver=` | engine | best at |
|---|---|---|
| `cvc5` (default) | cvc5 SMT, in-process (`pass/lec`) | designs where yosys SAT times out (e.g. 32-entry register files); cross-front-end checks; register correspondence by name |
| `bitwuzla` | in-process SMT (not yet built) | reserved |
| `lgyosys` | yosys `lgcheck` (SAT: equiv_make/simple/induct + bounded miter) | drop-in Verilog LEC; mature; gate-level / yosys-origin netlists |

Inputs (both `--impl` and `--ref`): `verilog:` / `pyrope:` / `ln:` / `lg:`, or a
bare `.v`/`.sv`/`.prp` path (kind inferred). Verilog elaborates through
`--reader` (default `slang`, the direct SV→LNAST front-end; `--reader
yosys-slang|yosys-verilog` overrides). The cvc5/bitwuzla backends consume the
graphs in process; `lgyosys` re-emits them to Verilog and shells out to lgcheck.

Rule of thumb: the **cvc5** default for graphs, cross-front-end checks, and when
the yosys SAT blows up (memory/register-file equivalence is SAT-hard for
bit-blasting — cvc5 uses bit-vector reasoning and reachable-state unrolling);
**`--set lec.solver=lgyosys`** for Verilog-in-hand and gate-level netlists.

---

## The `lgyosys` backend (yosys lgcheck — the former `lhd check`)

```
lhd lec --impl verilog:net.v --ref verilog:gold.v --top foo --set lec.solver=lgyosys --workdir wc
```
- `--impl` / `--ref` take `KIND:PATH` (or a bare path). Non-verilog kinds compile to Verilog first.
- Internally runs `inou/yosys/lgcheck`: `equiv_make` + `equiv_simple` +
  `equiv_induct`, then a **bounded miter** (`LGCHECK_BMC_STEPS`, default 5).
- Exit 0 = equivalent; non-zero = `equiv_fail` (or timeout). See the per-run log
  under `<workdir>/logs/` and `<workdir>/lgcheck_bmc.log` for the counterexample.

---

## The `cvc5` backend (in-process SMT relational equivalence, the default)

```
# Any input kind; a verilog side elaborates through --reader (default slang).
lhd lec --impl impl.prp --ref ref.v --top foo --workdir wl
# Cross-front-end: read the same RTL through both readers and prove them equal.
lhd compile foo.sv --reader slang        --top foo --emit-dir lg:impl_lg --workdir wi
lhd compile foo.sv --reader yosys-slang  --top foo --emit-dir lg:ref_lg  --workdir wr
lhd lec --impl lg:impl_lg --ref lg:ref_lg --top foo --workdir wl
```
Output: `PROVEN equivalent`, `REFUTED (not equivalent)` + a counterexample, or
`UNKNOWN` + a reason (an unsupported op, etc.). Exit non-zero unless PROVEN.

### Options (`--set lec.<flag>=<value>`)

| flag | default | meaning |
|---|---|---|
| `lec.engine` | `ind` | `ind` = single-step inductive miter; `bmc` = unroll from reset; `ic3` (reserved) |
| `lec.bound`  | `20`  | BMC unroll depth (number of cycles from reset) |
| `lec.solver` | `cvc5`| backend: `cvc5` (in-process, the only SMT backend built) \| `bitwuzla` (in-process, not yet built) \| `lgyosys` (yosys/lgcheck) |
| `lec.witness`| `true`| print the counterexample on REFUTED |
| `lec.phase`  | `free`| BMC reset-phase: `free` \| `reset` \| `run` (see below) |
| `lec.reset_cycles` | `2` | `run` phase: cycles to hold reset before checking |
| `lec.reset`  | *(auto)* | explicit reset inputs `name[:lo\|:hi]`, comma-separated; overrides auto-detect |
| `lec.cross`  | `false`| also run the `lgyosys` (lgcheck) backend and assert the two agree |

Example: `lhd lec --impl lg:a --ref lg:b --top foo --set lec.engine=bmc --set lec.bound=32`

### The two engines

- **`ind` (default).** Cuts every flop: its Q is a current-state symbol shared
  across the two designs; its next-state `reset ? init : (en ? din : Q)` is a
  synthetic output. One cvc5 query proves *equal current state + equal inputs ⇒
  equal next state + outputs*. Sound, fast (single query). **Incomplete:** it
  quantifies over ALL states including UNREACHABLE ones, so two equivalent
  designs that resolve a don't-care differently on an unreachable state get a
  (sound) false `REFUTED`.
- **`bmc`.** Starts from the reset state (each flop's constant `initial`, or a
  fresh shared symbol for a reset-less flop) and unrolls `lec.bound` cycles,
  comparing primary outputs each cycle. Only reachable-from-reset states are
  explored, so it does **not** false-REFUTE on unreachable states. Bounded
  proof (like the `lgyosys` backend's bounded miter). Use it when `ind` REFUTEs a design
  you believe is equivalent.

### Reset-phase separation (`lec.phase`, `bmc` only)

Reset-asserted behavior and free-running behavior are best proved **separately** —
a design can match its reference while reset is held but diverge once released
(or vice-versa), and a single mixed unroll hides which. The `bmc` engine takes a
phase:

- **`free` (default).** The reset input ranges freely; the unroll mixes both
  behaviors (the original `bmc` semantics).
- **`reset`.** Every primary reset input is held **asserted** on every cycle and
  every cycle is mitered — proves the two designs agree *under reset* (same reset
  values, same reset-forced outputs).
- **`run`.** Reset is held asserted for `lec.reset_cycles` cycles with **no**
  miter (just to drive both designs into their reset state), then **deasserted**
  and the following `lec.bound` cycles are mitered — proves free-running
  equivalence, undisturbed by reset.

To fully verify a sequential design, run **both** `reset` and `run`; both must
PROVE. (Demonstration + regression: `pass/lec/tests/lec_phase_test.sh` — a +1 vs
+2 counter PROVEs only in `reset`, a reset-forced combinational output PROVEs
only in `run`.)

**Reset-input detection.** A reset input is found automatically as (a) any
primary input driving a flop's `reset_pin` (async resets, e.g. a reset
synchronizer), and (b) any input with a canonical reset name (`rst`, `reset`,
`rst_n`, `rst_ni`, …; synchronous resets are folded into `din` and carry no
structural marker). The asserted level follows the flop's `negreset` attribute,
or for name-detected inputs an `_n`/`_ni` suffix (→ active-low). Override with
`lec.reset=rst_ni,clr_i:hi` when the heuristic mislabels an input. The verdict
detail line reports the resolved phase and warns if no reset input was found.

### Register correspondence (how the two sides line up)

`lhd lec` matches each flop of `impl` to the flop of `ref` by `flop_state_key`:
the **register name** (normalized — a leading `$..$` yosys decoration and a
trailing `___ssa_N` are stripped), then the **source span**, then the node id.
Both front-ends preserve the RTL register name (yosys-slang on the pin; the
native slang tolg stamps it), so corresponding registers collapse to one shared
state symbol. A register that can't be matched (unnamed, pass-inserted) becomes
a per-design symbol → a sound `UNKNOWN`/`REFUTED`, never a false `PROVEN`.

### Cross-front-end check (the common use)

Reading the *same* RTL through both readers and proving them equal validates the
native reader against yosys-slang — the graph-level analogue of "lgcheck against
the original":
```
lhd compile dut.sv --reader slang       --top dut --emit-dir lg:i --workdir wi
lhd compile dut.sv --reader yosys-slang --top dut --emit-dir lg:r --workdir wr
lhd lec --impl lg:i --ref lg:r --top dut --set lec.engine=bmc
```

---

### Memories (M4, SMT array theory)

A `Memory` cell is cut like a flop: its contents are an SMT array (`QF_ABV`,
`select`/`store`), and **corresponding memories of the two designs collapse to
one shared array symbol** — matched by a reader-invariant signature
(`size`×`bits`, read/write port counts) so the same RTL array read through both
front-ends lines up. The cut proves equal read douts + equal post-cycle
contents. The `wensize` difference between the readers (native word-enable vs
yosys-slang per-bit-enable) is normalized to a per-bit write mask, and a 0-cycle
(async) read is tied to `select(contents, addr)` via a deferred equality so it
composes with the combinational cone. This makes a large register file — where
the `lgyosys` backend's yosys-equiv bit-blasts and times out — tractable in one array
query. Best run with `lec.engine=ind` (the `bmc` array unroll can hit the cvc5
CaDiCaL crash below). Regression: `pass/lec/tests/lec_mem_test.sh`.

## Current limitations (pass/lec)

- Unsupported by the encoder: `Fflop`, `Latch`, `Sub` (instances, M5).
- **Memory `init` contents** are not seeded — the BMC/ind initial array is an
  unconstrained symbol (sound: starts both designs from the same arbitrary
  contents). Power-on-ROM equivalence would need the `init` pin modeled.
- **`ind` incompleteness** — false-REFUTE on unreachable states (use `bmc`).
- **cvc5 CaDiCaL crash** — cvc5's bundled CaDiCaL subsume/elim inprocessing can
  segfault on some `bmc` instances (a cvc5 solver bug, not the encoding). `ind`
  is unaffected; if `bmc` crashes, lower `lec.bound` or fall back to `--set lec.solver=lgyosys`.

## Pointers

- Engine: `pass/lec/{encode,query}.{hpp,cpp}`, design doc `pass/lec/lec.md`.
- CLI: `lhd/lhd_kernel.cpp` (`lec_command`, `load_side_graphs`).
- Tests: `pass/lec/` (`comb_equiv_test`, `lec_cross_test`).
