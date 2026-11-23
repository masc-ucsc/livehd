# Running a program on a design: `cert_run`

`cert_run` steps a design cycle by cycle on a proved simulator until a stop condition. With a
bind file (`bind.md`) it serves the design's memory buses, so a CPU design runs a program image
(an ELF file, a raw binary or `$readmemh` words) until the program writes its exit register.
Without one it runs open loop, like `cert_trace`, with stop conditions, program loading into the
design's own memories, VCD and lockstep added.

```
cert_run <cert> [bind:<file>] [stim:<file>] [edges:all|edges:third|edges:file:<path>]
         [route:ir|vc|ref] [opt:off] [init:reset|init:zero] [ckpt-in:<file>]
         [load:<image>[@<addr>][:mem<k>]] [vaddr] [word:<bytes>]
         [max:<cycles>] [stop:<output>=<value>]
         [trace:<dir>] [vcd:<file>] [lockstep:<route>]
         [ckpt-at:<cycle>:<file>] [ckpt-out:<file>] [compact:<cycles>]
```

## Options

- `route:` picks the simulator. `ir` (the default) is the direct simulator, `vc` the verified
  compiler, `ref` `interpretDesign` itself. `ir` runs the staged direct step
  (`Compiler/DirectStaged`): the design is prepared once, each node specialized to its operator
  with its constant operands folded in, and every slot kept as its reduced value;
  `Staged.step_eq` (`Compiler/DirectStagedSound`) proves the step equal to `directStepRaw`. `ref` is exact by definition and slow on designs
  with memories. `opt:off` runs `vc` without its optimizer.
- `route:fp` runs the Futamura projection's residual, the route `dcert2_memroute` runs (the
  fused memory interpreter, plain writes admitted, the step with its list tails shared,
  `Projection/ShareTails`; `opt:off` runs the residual as specialized). The residual is
  specialized and checked once, at the budget `dcert2_memroute auto auto` uses. Each step
  converts the state to the residual's (`Ckpt.save`, then `CkptStateM.toStateM`: each memory a
  table below its depth), checks the runtime policy and runs the residual. Its outputs are
  `interpretDesign`'s and its state agrees with `interpretDesign`'s on every flop and on every
  memory word below its depth (`Run/Sound.lean`: `fpStep_agree`, `mkStepper_fp_agree`,
  `runCycles_agree`), so its traces and checkpoints are the other routes' (the selftest compares
  them). It refuses a certificate the direct check, the address, depth, image-root or word
  checks refuse, or one the memory route does not admit.
- `bind:` adds a host: reset, constants, buses, memory, console, exit, registers, stop
  conditions.
- `stim:` gives inputs from a `stim v1` file (`cert_trace`'s format, absolute cycles). The bind
  file's constants, reset and bus inputs override it.
- `edges:` says which clocks fire each cycle, as in `cert_trace`. The default fires every clock.
- `init:reset` (the default) starts every flop at its reset value and every memory at its
  power-on image. `init:zero` starts everything at 0. `ckpt-in:` starts from a checkpoint and
  restores the host from its `cert_run host v1` section.
- `load:<image>` loads a program into the host memory. The type comes from the extension:
  - `.bin` is raw bytes at `@<addr>`.
  - `.hex` is `$readmemh` words of `word:<bytes>` bytes (default 4) at their word addresses.
  - Anything else is read as ELF: 32- or 64-bit, either endianness, any machine. Its `PT_LOAD`
    segments go to their physical addresses (`vaddr`: virtual addresses). A dynamic or
    interpreted executable is refused.
- `load:<image>[@<addr>]:mem<k>` loads into the design's own memory `k` instead. Byte `a` lands
  in word `(a - addr) / (dw / 8)`, little-endian. A `.hex` file's words go to its word
  addresses plus `addr`. A word at or past the memory's depth is refused.
- `max:` bounds the run (default 1,000,000 cycles). `stop:<output>=<value>` stops when the
  output has the value, sampled before the edge, like a bind `stop-output`.
- `lockstep:<route>` runs a second route on the same ticks and stops with `lockstep` at the
  first cycle whose outputs differ.
- `ckpt-at:` and `ckpt-out:` write `lhdcert-ckpt v1` checkpoints (`ckpt.md`). A bind run adds
  its host section.
- `compact:<K>` replaces the simulator state by its restored checkpoint every `K` cycles, in
  memory. The `ir`, `vc` and `ref` routes hold a memory as a function: each cycle's image calls
  the previous cycle's, and keeps that cycle's evaluated slots alive. Without compaction, a
  run's memory use grows every cycle and a memory read walks back to the run's start, even
  when nothing is written. A restored state holds each memory as one table, so the growth and
  the read cost are bounded by `K` cycles. The outputs do not change (below). Default: off.

## Output

The summary lines are `status`, `cycles` (the cycle the run stopped at), `applied` (the cycles
whose edge was committed), then `exit_code`, `console`, one `word <name> <hex>` per register,
the bus statistics, `wall_ms` and `cycles_per_s`. The last line is `RESULT: <status>`.

Statuses: `exit`, `stop-output`, `limit`, `breakpoint`, `watchpoint`, `protocol`, `mealy`,
`bus-error`, `rom-write`, `lockstep`, `refused`. `bind.md` says which stop before the cycle's
edge and which after it.

Exit code:
- 0 for `exit` with code 0, `stop-output`, `limit`, `breakpoint` and `watchpoint`;
- 1 for `exit` with another code and for the failure statuses;
- 2 for a usage error;
- 3 when the design, an image or a checkpoint is refused.

`trace:<dir>` writes:
- `steps.trace`: one `STEP <k> <outputs>` line per applied cycle, `cert_trace`'s format.
- `inputs.stim`: every applied input that is not 0, as `stim v1` `at` lines.
- `edges.edges`: the clocks that fired, when not every clock fires every cycle.
- `bus.trace`: one line per transfer: arrival cycle, answer cycle, `I`/`R`/`W`, address, data,
  strobes (hex).
- `console.txt`: the console bytes.

## Why a run can be trusted

The host chooses each cycle's inputs from the design's outputs. What it cannot change is how
the design steps. `HDLProof/Run/Sound.lean` proves:
- `cycleStep_applied`: every applied cycle is one `interpretDesign` step on the tick it records.
- `runCycles_sound`: for every host (any bind file, image, latency and stimulus), a run's outputs
  and final state are those of `refTrace`, iterated `interpretDesign`, on the recorded ticks.
- `mkStepper_ir_sound` (and `refStep_sound`): the `ir` (and `ref`) steppers agree with
  `interpretDesign`. For `vc` this is `compileDesign_correct` and `compileOpt_correct`.
- `compacted_outputs`: a run cut into segments, with its state replaced by the restored
  checkpoint of itself after each one (`compact:`), prints the outputs of the run that is not
  cut. It chains `resume_exact` (`Projection/CkptResume.lean`) over the segments.

The driver iterates the same `cycleStep`. So the recorded `inputs.stim` replays the run open
loop: `cert_trace <cert> <cycles> inputs.stim` prints the run's `steps.trace` on every route.
`tests/run_selftest.sh` checks this on every run it makes.

## Limits of this version

- One bus protocol, valid/ready with a single outstanding request per bus. AXI or other
  pipelined buses need a host of their own.
- Outputs must not depend on `ready` or `rdata` in the same cycle (Moore at the bus).
- A design's own memories can be loaded, but the host cannot reach into them during the run.
