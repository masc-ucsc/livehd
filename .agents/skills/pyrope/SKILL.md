---
name: pyrope
description: Write, check and improve Pyrope. Use for Chisel or Verilog translation, configurable hardware, Pyrope semantics, and source-level synthesis optimization.
---

# Pyrope

Pyrope elaborates to wires, muxes, flops and memories. For the shared compiler,
simulation, proof and placement workflows, see the [LiveHD skill](../livehd/SKILL.md).

| part | read it when |
| --- | --- |
| [semantics.md](semantics.md) | writing or reviewing any Pyrope: declarations, types, lambdas (`comb`/`pipe`/`mod`), tuples, bits, registers and timing, `wire`, memories, verification, tests, Verilog mapping, gotchas, not-yet-implemented features |
| [cleanup.md](cleanup.md) | translating a hardware generator, consolidating emitted variants, and verifying a refactor or a requested interface change |
| [synth.md](synth.md) | improving area/frequency: `synth.*` attributes, measurement method, critical-path driven levers, source rewrites that map better |

## Rules that apply to every task

* Verify every edit with `lhd`: compile, then the strongest check available
  (LEC against the previous version or a reference, simulation, formal).
  `lhd compile` passing is necessary, not sufficient.
* Never weaken a check, change a reference, or alter the reference setup to get
  a pass. `refuted` is a bug to find; `unknown`/timeout is inconclusive and
  needs simulation behind it.
* Use an explicit initializer to communicate intent (`reg q:U8 = nil` has no reset);
  narrowing assignments need `wrap`/`sat`; `Bool` and integers never mix; type
  words are capitalized (`U8`, `S4`, `Bool`, `Clock`, `Reset`); clocks and
  resets bind by TYPE, not by name.
* `match` is parallel and exclusive, not priority (use `if/elif` for
  priority). `@[N]` checks a cycle, `stage[N]` inserts flops. Outputs are named
  and assigned; `return X` does not exist.
* Synthesis attributes (`::[synth.*]`) never change function; prove the result
  anyway. Annotate the Pyrope source, not generated files.
* Reproduce surprising behavior on the current compiler and check the language
  reference. For a supported construct, fix the failing compiler stage and add a
  regression that exercises it. Ask before changing language semantics.

Use `lhd compile` to check lowering and `lhd sim` to exercise Pyrope test
blocks. Design assertions belong in a formal check. An `unsupported` diagnostic
can identify valid Pyrope awaiting lowering. Reduce an unexplained internal
error to a small reproducer and check its cycle, clock and reset declarations.
