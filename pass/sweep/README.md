# pass/sweep -- whole-design clean-up across module ports

The per-module compile passes (cprop, bitwidth, enableopt) stop at every
instance boundary: a constant on an input port, an output nobody reads and a
register nothing can observe are all invisible to them, and a design built from
hundreds of copies of a parameterised module (a NoC, a router mesh) keeps every
copy's full datapath even when most of the datapath can be proven constant or
dead. Yosys `flatten` + `opt` removes that; this directory is the LiveHD
equivalent, **opt-in** and wired into `lhd compile` / `lhd synth`:

| option | default | effect |
|---|---|---|
| `compile.flatten` | `false` | after the first cprop+bitwidth, splice every instance that has a body into the selected top (or each uninstantiated root), recursively; the original definitions are dropped once nothing instantiates them. Black boxes (macros, Liberty cells) and rolled loops stay instances. |
| `compile.unknown_zero` | `false` | read every constant bit that is unknown (`x`: an undriven output, an unconnected input, an explicit `'x`) as 0 before the following cprop, like `setundef -zero` run *before* optimization. Flop `initial`, memory and instance pins keep their unknowns. A legal refinement of a don't-care. |
| `compile.sweep` | `false` | after the last cprop+bitwidth, iterate `sweep_state` (below) with cprop+bitwidth in between until a round finds nothing. |

All three are `compile.*` kernel gates: `lhd compile`, `lhd synth` and `lhd lec`
accept them as `--set compile.flatten=true`. They are part of the compile-cache
key (the whole `--set` list is) and of `//lhd:compile_salt`.

Pair them with `pass.abc.ware_zero_bits=true` (an extracted ware shifter sees the
operand bits its parent drives with a provable 0) when the flattened design is
mapped; both are described in `pass/abc/README.md`.

## State sweep (`state.cpp`)

`sweep_state(graph)` works on one graph (use it on the flat top) and removes two
kinds of flop bit. It reads the shapes cprop leaves behind (Mux, Hotmux,
Get_mask, Concat, And/Or/Xor/Not, EQ, Ror, Sum, constant or narrow-index SHL/SRA)
and treats every other shape as "anything", so it can miss a fold but never
invent one.

**Constant bits, by induction.** Hypothesise that every candidate bit already
holds its constant -- the reset value for a flop with a known reset, and a free
choice (0, then 1) for a flop with neither reset nor `initial` -- and evaluate
each bit's next state under that hypothesis, with the other bits' constants
included. The select of a Mux, the controls of a Hotmux and the enable of a flop
are folded the same way, and the reset net a group of flops shares is taken as
inactive while their next states are evaluated (when it is active they hold the
reset value, which is the claim). A bit whose next state is not provably its
constant falls out of the hypothesis; repeat until nothing falls out. What
remains is an inductive invariant, so a ring of FIFOs whose `full` bits keep each
other at 0 is proven at once; a pessimistic per-flop test would never see it.
A flop with no reset and no `initial` is "free to start at its constant": the
usual synthesis refinement of power-on contents (Yosys `opt_dff`), and the reason
this is not on by default.

**Unobserved bits.** A backward demanded-bits analysis from the graph outputs,
instances, memories, latches and every other node that is not plain logic or a
depth-1 flop. A flop bit nothing demands is removed -- including the register
that only feeds itself, or only other unobserved registers, which per-node dead
code elimination cannot see. Shifts are bit-precise: `data_i[idx]` (a right shift
of the packed array by an affine function of a narrow index) demands only the
lanes the index can select, so the unread payload of an arbiter's inputs dies.

**Rebuild.** A flop with constant or dead bits is replaced by flops over the
contiguous runs of live bits (named `reg[hi:lo]`, `reg[i]` for one bit) and a
Concat that puts the constants (or 0, for an unobserved bit) back for the old
readers; a flop with nothing live disappears. A signed register is only replaced
whole.

## Measured

`floonoc_mesh_top` (HighTide asap7, 4x4 FlooNoC mesh with black-box cluster stubs
whose outputs are undriven), `lhd synth` without placement:

| options | flops | logic cells |
|---|---:|---:|
| default | 352,780 | 2,629,820 |
| `compile.flatten` + `compile.unknown_zero` + `compile.sweep` + `pass.abc.ware_zero_bits` + `pass.abc.memory_prune_reads` | 6,188 | 81,682 |

The default flow does not finish placement in 64 GiB / 7200 s; with the options
the whole harness flow runs in 5271 s (peak 5.9 GiB) and places 87,872 cells
against 91,026 for the OpenROAD reference. Yosys `flatten; opt` with `setundef
-zero` first leaves ~4.3k flops and ~24k logic cells on the same source; the
remaining difference is hbm_ni meta-buffer state and router control bits the
evaluator's shape coverage does not yet fold.
