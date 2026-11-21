# The `bind v1` host file

A bind file tells `cert_run` how to drive a design and how to serve its memory requests. With it,
a design that fetches instructions and data over a bus runs a program image to its end.
`HDLProof/Run/Host.lean` parses it, and `HDLProof/Run/Loop.lean` runs it.

Ports are named as in the `_io.json` beside the certificate. A number names a port by its
ordinal. Numbers are decimal or `0x` hex. `#` starts a comment.

```
bind v1
reset <input> <active-value> <cycles>   hold the input at the value for the first cycles
const <input> <value>                   drive the input with a constant
bus <name> valid=<out> addr=<out> ready=<in> rdata=<in> [wdata=<out>] [wstrb=<out> | we=<out>]
           [instr=<out>] [latency=zero|fixed:<n>|random:<seed>:<max>]
ram <base> <size>                       host memory, read and written by every bus
rom <base> <size>                       host memory; a write to it stops the run (rom-write)
console <addr>                          a write appends its low byte to the console
exit <addr>                             a write stops the run (status exit; the data is the code)
word <addr> <name>                      a register: a read gives the last value written (0 at first)
stop-output <out> <value>               stop (stop-output) when the output has the value
break <addr>                            stop (breakpoint) when an instruction fetch asks for the address
watch <addr>                            stop (watchpoint) after a data access to the word completes
```

After the reset cycles, the reset input takes the other value: 1 when the active value is 0,
else 0.

## The bus

A bus is the simple valid/ready handshake that small cores use:
- The design raises `valid` with the request fields: `addr`, and for a write `wdata` and the
  strobes. It holds them until the host answers.
- The host answers after the bus's latency: it raises `ready` for one cycle. For a read,
  `rdata` holds the word in that cycle.
- With `latency=zero` the host answers in the cycle the request appears. `fixed:<n>` waits `n`
  cycles. `random:<seed>:<max>` draws each wait from 0 to `max` with a seeded xorshift
  generator, so a run repeats exactly.
- A write is `wstrb != 0` (bit `i` writes byte `i` of the word) or `we = 1` (every byte). Any
  other request is a read. `instr = 1` marks a fetch; it matters only for `break` and for the
  statistics.
- Addresses are byte addresses. The data word is the `rdata` port's width, which must be a
  multiple of 8. A request reads or writes the aligned word that holds its address. Words are
  little-endian.

The host decides each cycle from the outputs computed with `ready = 0` and `rdata = 0`. So the
design's outputs must not depend on `ready` or `rdata` in the same cycle. `cert_run` checks this
every cycle that a request completes. If an output differs it stops with `mealy`.

These stop the run before the cycle's edge:
- `protocol`: a pending request's fields changed, or `valid` fell, before the answer.
- `bus-error`: an answered address names no region and no device.
- `stop-output`, `breakpoint`.

These stop it after the edge: `exit`, `rom-write` (the write is dropped), `watchpoint`.

## Memory

`ram` and `rom` regions must not overlap. A device address is checked before the regions. A
program image is loaded into the regions before cycle 0 (`cert_run load:`). A rom region can be
loaded, only the design cannot write it.

## In a checkpoint

The host state is part of a run's state: the memory regions, each bus's pending request, the
register values, the console, the exit code, the random generator and the statistics. A
checkpoint that `cert_run` writes keeps it in the ext section `cert_run host v1`. Resuming with
`ckpt-in:` needs the same bind file; a host section whose regions or buses differ is refused.
