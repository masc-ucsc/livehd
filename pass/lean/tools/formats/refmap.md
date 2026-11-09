# The `.refmap` format: refinement maps and spec facts

A `.refmap` file says how an implementation certificate refines a spec certificate with
stuttering. `drivers/sym_refmap.lean` reads it in two modes:

```bash
sym_refmap refmap    <file.refmap> [jobs:<n>] [runs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>] [cache] [solver:<cadical>] [work:<dir>]
sym_refmap specfacts <file.refmap> [jobs:<n>] [sf:<n>] [wf:<n>] [emit:[f1:|f2:]<file>] [cache] [solver:<cadical>] [work:<dir>]
```

## The model

The **spec** is a certificate stepped once per **event**. Each event is on one **channel**. A
channel is a set of constant spec inputs that select it, the spec inputs that take its response
words, the spec output that says whether it is allowed, and the spec outputs it presents (its
observed words).

The **map** (`alpha`) gives the spec's state at every implementation cycle: spec flop `k` is
implementation output `alpha[k]`, normalized to its width. The implementation needs observer
outputs for this; `scripts/sym_observe.py` adds them to a copy of the RTL.

A **slot** is an implementation event. It names its channel, the formula over real ports that
fires it, the ports its response words come from, and the words it must present. Slots fire in
file order within a cycle, so one cycle can hold several events (several commits; a bus
handshake and a commit report).

Per cycle `n` from `reset` on, the check proves:
- **allow**: each slot that fires is allowed in the spec state reached so far in the cycle;
- **obs**: it presents the spec's observed words;
- **comm**: the spec run over the fired slots ends in the map's state at `n+1`. A cycle in
  which no slot fires is a stutter: the map must not move.
- **active**: the `active` output keeps holding.
- **rank** (per ranked channel that is allowed in the map's state and gets no event in the
  cycle):
  - `rankLe`: the rank does not rise;
  - `rankLt`: it falls on a helpful cycle (the selector is 0, internal work, or the requirement
    it names fires);
  - `rankSel`: when it stays, the selector stays and its requirement is enabled.

The **base** proves that `active` and the `init` constraints hold at cycle `reset`, from the
all-zero state. The implementation's **invariant** (`inv`) is proved first, by the
conjunctive checker. Every step obligation assumes it at cycle `n`.

The **spec facts** are checked once per spec, independently of any implementation:
- **moore**: per channel, the allow output and the observed words do not depend on the
  response;
- **persist**, **outC**, **comm**: per pair of channels both allowed in a state, each event
  leaves the other allowed and its words unchanged, and the two orders reach the same state.

With the facts, each channel's stream is a function of the environment's answers (Kahn
determinism). Two implementations that refine one spec show the same streams.

## Directives

One directive per line; `#` starts a comment. Names are those of the certificates'
`_io.json` sidecars. Paths are relative to the `.refmap` file.

| directive | meaning |
|---|---|
| `impl <cert>` | the implementation certificate (not read by `specfacts`) |
| `spec <cert>` | the spec certificate (memory-free) |
| `inv <file>` | the implementation's invariant, in the `sym_verify conj:` format; none means no invariant |
| `asm [!]<output>` | a cycle assumption on an implementation output |
| `in <input>=<value>` | a cycle assumption on an implementation input (repeatable) |
| `reset <n>` | the first refined cycle (default 1) |
| `active [!]<output>` | an implementation output that holds from `reset` on (checked) |
| `alpha <spec-reg> <output>` | the map for spec register `<spec-reg>` (every spec register needs one; `specfacts` reads none) |
| `init <spec-reg> <int>\|out0:<output>\|free` | the map's value at `reset`: a constant, implementation output `<output>` at cycle 0, or unconstrained (default) |
| `chan <name> allow:[!]<spec-out> [sel:<spec-in>=<v>,...] [resp:<spec-in>,...] [obs:<spec-out>,...] [rank:<rank-out>/<sel-out>]` | a channel. `rank` names implementation outputs: the rank, and the selector (0: internal work; `j+1`: waiting on requirement `j`) |
| `slot <chan> <bool> [resp:<port>,...] [obs:<word>,...]` | a slot, fired by `<bool>`; named `<chan>#<index>` in reports |
| `req <name> en:<bool> fires:<bool>` | an environment requirement, in file order (`j`): enabled, and its helpful action |
| `group <spec-reg>,<spec-reg>,...` | spec registers checked in one comm obligation; every other register is checked alone |
| `split <conjunct>` | a `oneof` conjunct of the invariant to split every step obligation on |

A `<bool>` has no spaces: `!`, `&`, `\|`, parentheses; atoms `<port>` (nonzero), `<port>==<n>`,
`1`, `0`. A `<port>` is `out:<name>` (cycle `n`), `out:<name>@next` (cycle `n+1`) or
`in:<name>` (cycle `n`).

A `<word>` is `<port>`, or a slice `<port>[lo:len]`, optionally guarded: `<word>?<port>.<bit>`
reads 0 unless that bit is set.

### Lines read only by the judge and the product search

`refmap` and `specfacts` ignore these. `sym_refmap judge` (one concrete run: does ANY stuttering
alignment of the spec match the implementation?) and `scripts/mk_product.py` (the product model
for a bounded search) read them, with `impl`, `spec`, the one `chan`, the one `slot` and `asm`.
They need no `alpha`, `init`, `group` or `inv`.

| directive | meaning |
|---|---|
| `xfer <bool>` | an implementation transfer (repeatable): the spec may not hold (stutter) in a step where one is true |
| `maxstall <n>` | at most `n` holds in a row, in steps (default 0: lockstep) |
| `period <P> [<k>]` | random runs only: inputs change only at steps `t` with `t % P = k` (default `k` = 1 when `P` > 1: the real clock's input timing on a `single_edge` certificate with P slots); the product adds the same rule for P = 2 to `env_ok` |
| `drive <input> <v0> <n> <v1>` | random runs only: the input is `v0` before step `n` and `v1` from step `n` on (a schedule `asm` requires, e.g. a reset) |

An output channel the change delays without a ready is read late: `out:<name>@next` in the slot's
`obs`, for its valid and every word it guards, and in the `xfer` lines that read its valid
(`xfer out:<valid>@next`). The judge warns about a word whose payload and guard are read at
different cycles, and about an `xfer` that reads an output at a cycle the slot never observes it
at.

The judge's results: `NO MATCH at step n` (no alignment exists on the run UNDER THIS CONTRACT: the
report lists every last candidate and why it fails), `MATCH FOUND` (and, if the slot's own stall
choice fails on the run, the step where it does: STALL CHOICE WRONG), `ENVIRONMENT VIOLATED` (no
random vector kept `asm` at a step). A NO MATCH is not by itself a bug: a contract that reads a
delayed channel in the same cycle, or allows fewer holds than the change adds, rejects a right
implementation too. Each NO MATCH comes with the run's inputs as a trace, whether the run kept
`asm`, the failing channels as transactions (D0's words, and D1's transactions on the channel
around the step), and a `CLASS:` hint from re-reading the same run under relaxed contracts:
`DELAY?` (the channel read later removes it), `STUTTER?` (a larger `maxstall`, or one `xfer`
left out, removes it) or `VALUE DIFFERENCE` (none does). A delay or a wait counts only if the
change adds it, which the RTL decides. Random runs keep `asm` (a vector that breaks it is drawn
again, `tries:`), and the transactions searched are counted per channel: a MATCH FOUND on runs
with no transaction on a channel says nothing about that channel.

Controls, `sym_refmap judge <file.refmap> <runs> control:pos|neg [delay:<out>=<k>,...]
[flip:<out>@<valid>,...]`: the same contract with D0 run as the implementation (by the spec
certificate's port names; the slot fires always). `pos`: D0 with each `delay:` output passed
through `k` registers (the delays the change adds, from the RTL) must MATCH on every run; a NO
MATCH means the contract is stricter than the change allows. `neg`: D0 with one output bit
flipped at a time (`flip:` bit 0 of `<out>` while `<valid>` is 1; by default bit `lo` of every
observed word, every cycle) must NO MATCH; a flip made but never seen is a word the contract
does not compare. A channel with no transaction on the positive control's random runs fails
the controls (on a trace, the counts are reported only). Neither needs a separate certificate.

## Example

```
impl alt_lean/alt.dcert
spec two_lean/two.dcert
reset 0
alpha ra ra_o
alpha rb rb_o
init ra 0
init rb 0
chan a sel:sel=0 resp:x obs:ra_o allow:ok rank:rk_a/sl
chan b sel:sel=1 resp:x obs:rb_o allow:ok rank:rk_b/sl
slot a out:ph_o resp:in:x obs:out:ra_o
slot b !out:ph_o resp:in:x obs:out:rb_o
```

`tests/refmap_selftest.sh` builds this and the other fixtures, including the failing ones.

## Results

| result | meaning |
|---|---|
| `REFINEMENT PROVED` | `runRefMapOf` accepted the proofs: `runRefMapOf_sound` gives `RefMapFacts` on every run |
| `SPEC FACTS PROVED` | `runSpecFactsOf` accepted the proofs: `runSpecFactsOf_sound` gives `SpecFactsOK` |
| `NOT INDUCTIVE (invariant)` | an invariant conjunct is not preserved; fix `inv` (see `sym_verify conj:`) |
| `NOT A REFINEMENT (step): [targets]` | the named targets have a counterexample from an invariant state |
| `COUNTEREXAMPLE (base)` | `active` or `init` fails at `reset` |
| `SPEC FACTS FAIL: [targets]` | the spec is not Moore, or two channels do not commute |
| `REFUSED` | a side condition or the specialization failed |

A step counterexample is written to `<work>/cti_<target>.txt`. It lists the slots that fired,
the nonzero inputs of both cycles, the spec registers on which the spec after the cycle
differs from the map at `n+1`, the map at `n`, and the implementation's registers at `n`. A
target name longer than 120 characters (a group lists its registers) is cut to its first 80,
followed by `__target<index>`.
`<work>/refmap_table.tsv` (`specfacts_table.tsv`) has clauses and solver time per obligation.

`cache` reuses an LRAT proof already in `<work>` when the obligation's CNF there is
byte-identical, without running the solver. The verified check re-checks every proof
either way, so the cache saves solver time only. A satisfiable or failed obligation leaves no
LRAT file (the solver writes one on any answer), so a counterexample is found again. Use it to
emit a verdict file from a round that has already solved everything.

## Theorem files

`emit:` writes a Lean file stating the verdict on the fused route:
`Check.RefMapChecked sf wf sf wf DI DS spec s0` (`Check.SpecFactsChecked sf wf DS spec`). The
certificates are included as text (`include_str`, paths relative to the file) through
`Check.certOf`, so two files that include the same spec text name the same spec certificate.

- `emit:f2:<file>` embeds the proofs and proves the statement by `native_decide`: the axioms
  are the standard ones plus one `native_decide` axiom.
- `emit:f1:<file>` states it as one named axiom `checked`. The axiom's docstring records the
  SHA-256 of the certificates and of the proofs. Use this for proofs too large to embed.

`Check.RefMapRef` turns such statements into theorems over `interpretDesign`'s outputs
(`refOut`):
- `refmap_compose_ref`: two maps of one spec, the spec's facts, the same start and one
  environment `E` give equal streams on every channel;
- `start_ref`: their starts agree when their `init` lines do (`InitAgree`);
- `refmap_progress_ref`: under justice, every allowed ranked channel gets an event.

The selftest's `thm/Compose.lean` is a complete example.
