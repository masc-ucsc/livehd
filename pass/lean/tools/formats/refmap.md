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
either way, so the cache saves solver time only. Use it to emit a verdict file from a round
that has already solved everything.

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
