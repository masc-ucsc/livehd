---
name: pyrope
description: Write, check, clean up and synthesis-optimize Pyrope (the LiveHD hardware description language) with the lhd tool. Use when creating or editing .prp files, translating Verilog to/from Pyrope, answering Pyrope syntax/semantics questions, running lhd (compile, sim, formal, lec, synth, sta), cleaning machine-emitted Pyrope, or improving area/frequency with synthesis attributes.
---

# Pyrope

Pyrope is a hardware description language: every construct elaborates to
wires, muxes, flops and memories. `lhd` (LiveHD) compiles, simulates, proves,
synthesizes and times it. This skill has four parts; read the one the task
needs (they reference each other):

| part | read it when |
| --- | --- |
| [semantics.md](semantics.md) | writing or reviewing any Pyrope: declarations, types, lambdas (`comb`/`pipe`/`mod`), tuples, bits, registers and timing, `wire`, memories, verification, tests, Verilog mapping, gotchas, not-yet-implemented features |
| [lhd.md](lhd.md) | running the tool: command contract, artifacts (`ln:`/`lg:`), sim, formal, lec verdicts, synth, critical-path STA (`pass opentimer` + `tool sta`), fmt/style, diagnostic triage |
| [cleanup.md](cleanup.md) | turning emitted or legacy Pyrope into idiomatic code while proving it unchanged |
| [synth.md](synth.md) | improving area/frequency: `synth.*` attributes, measurement method, critical-path driven levers, source rewrites that map better |

## Rules that apply to every task

* Verify every edit with `lhd`: compile, then the strongest check available
  (LEC against the previous version or a reference, simulation, formal).
  `lhd compile` passing is necessary, not sufficient.
* Never weaken a check, change a reference, or alter benchmark settings to get
  a pass. `refuted` is a bug to find; `unknown`/timeout is inconclusive and
  needs simulation behind it.
* Every data declaration needs `= value` (`nil` = no value/no reset);
  narrowing assignments need `wrap`/`sat`; `Bool` and integers never mix; type
  words are capitalized (`U8`, `S4`, `Bool`, `Clock`, `Reset`); clocks and
  resets bind by TYPE, not by name.
* `match` is parallel and exclusive, not priority (use `if/elif` for
  priority). `@[N]` checks a cycle, `stage[N]` inserts flops. Outputs are named
  and assigned; `return X` does not exist.
* Synthesis attributes (`::[synth.*]`) never change function; prove the result
  anyway. Annotate the Pyrope source, not generated files.
* When the compiler disagrees with this skill, trust a minimal reproducer on
  the current build over any remembered rule, and report the discrepancy.
