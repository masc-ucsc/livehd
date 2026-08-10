# todosim — `lhd sim` vs Verilator on minion

Working notes for the minion simulation bring-up. Everything below was measured
on 2026-08-09 against the working tree of `../livehd` (uncommitted), Verilator
5.050, an 18-core M-series box.

The short version: **the Verilog correctness gate passes in this working tree.** On a ROM that
enables floating point and executes FMV/FMADD through txfma, `lhd sim` and
Verilator produce byte-identical top-level IO and enabled-write traces for
20,000 cycles, both with clock gating enabled and with all three chicken bits
set. Pyrope-path parity and a fresh performance pass remain.

---

## 1. Goals

1. **`lhd sim` Verilog == Verilator Verilog** on minion_top, with and without the
   clock-gating chicken bits, over a program that exercises more of the CPU
   (including a txfma op). This is the gate — everything else is downstream.
2. **`lhd sim` Pyrope == `lhd sim` Verilog.** Regenerating `minion/pyrope/` is
   allowed if that is what it takes.
3. Close the speed gap, or at least understand it. dino sits at 1.27x of
   Verilator; minion at 14.1x, so the gap is design-scale, not a constant.

Only the *top-level IOs* and *memory writes (when write-enable is set)* have to
match. Individual flops may legitimately differ because of clock gating.

---

## 2. Status

### Correctness — PASSING

| simulator | retired @20k | last_pc | done |
| --- | --- | --- | --- |
| `lhd sim` verilog (`lg:` from slang) | 9,397 | 37,580 | cycle 46 |
| Verilator | 9,397 | 37,580 | cycle 46 |

The complete 20,000-row canonical trace hash is
`f9ee060d569981784029076a5362b12b6980a8310901139d55e228128f711e21`
for both simulators in both gate configurations. The trace contains every
top-level output and includes payloads only when their valid/write-enable is
set. An LHD internal probe also observes txfma valid and clock-valid at cycle
45; FMV retires at cycle 47 and FMADD at cycle 49.

The final fixes were in `inou.cgen.sim`: nested post-rise refresh for the
two-read preview register file, and preservation of a Hotmux's full one-hot
selector width independently of its result width. Slang also needed named
structured-assignment-pattern lowering for the Minion sources.

### Speed — needs remeasurement

The figures below predate the correctness fixes. Repeat the two-point fit
`t(N) = startup + N/rate` over N = 2000 / 20000, best-of-5, no VCD:

| simulator | startup | cycles/s |
| --- | --- | --- |
| `lhd sim` verilog | 30.2 ms | 10,539 |
| Verilator (C++ tb) | 17.6 ms | 148,760 |

Build: Verilator 6.3 s (4.4 s verilate + 2.0 s `make -j18`); `lhd sim` ~10 s
setup plus a 173-TU host C++ compile that measured anywhere from 60 s to 186 s
run to run. For contrast dino is **1.27x** (lhd 3.40M vs verilator 4.32M
cycles/s). `--set sim.flatten=8` changes nothing here (still 173 TUs, 9,709
cycles/s — noise).

Always use a two-point fit: process startup is 17-30 ms, which is 12% of dino's
500k-cycle run and ~96% of minion's tensora_rf 100k run.

---

## 3. How to reproduce

`$L` is the lhd built from this tree. `RUNFILES_DIR` is required when running it
straight out of `bazel-bin` — otherwise the host compile dies with "could not
locate the sim runtime headers".

```bash
cd ~/projs/lhdsuite
bazel build @livehd//lhd:lhd
export L=$PWD/bazel-bin/external/livehd+/lhd/lhd
export RUNFILES_DIR=$L.runfiles
mkdir -p /tmp/mp/tree && cd /tmp/mp
cp -L ~/projs/lhdsuite/minion/pyrope/*.prp ~/projs/lhdsuite/minion/sim/*.prp tree/
```

### lhd sim, MODE=verilog (the apples-to-apples path vs Verilator)

```bash
$L compile verilog --top minion_top --emit-dir lg:lg_v --workdir tw_v -- \
    -F ~/projs/lhdsuite/minion/verilog/filelist.f -DSYNTHESIS \
    --relax-enum-conversions --allow-use-before-declare        # ~30 s
$L sim lg:lg_v tree/minion_prog_tb.prp --setup-only --set sim.vcd=false --workdir SW_v
$L sim lg:lg_v tree/minion_prog_tb.prp --run-only --arg cycles=20000 \
    --set sim.ninja=false --workdir SW_v                       # ~60-186 s (host c++)
./SW_v/sim/drv.bin --cycles 20000 | grep 'minion program:'
```

### Verilator

Run from `minion/verilog/` — the generated `intpipe_csr_file_auto_*.svh`
includes resolve relative to the including file.

```bash
cd ~/projs/lhdsuite/minion/verilog
verilator --cc --exe --Mdir /tmp/mp/vobj --top-module minion_top -Wno-fatal \
    -DSYNTHESIS -F filelist.f ../sim/minion_prog_tb_verilator.cpp
make -C /tmp/mp/vobj -f Vminion_top.mk -j18 Vminion_top
/tmp/mp/vobj/Vminion_top --cycles 20000
```

### Comparing internals

`--probe`, `--probe-from/-to` and `--debug-json` are **runtime** flags on
`drv.bin`, not setup flags — no rebuild needed to change the probe set.

```bash
$L sim lg:lg_v tree/minion_prog_tb.prp --list-signals --workdir SW_v   # 66,985 signals
./SW_v/sim/drv.bin --cycles 40 --probe "acc.u_core.u_intpipe.ex_ctrl,..." \
    --probe-from 0 --probe-to 40 --debug-json p.json
```

For Verilator, build with `--public-flat-rw` and read `dut->rootp->…`. lhd's
`acc.u_core.u_intpipe.wb_reg_xcpt` maps to
`minion_top__DOT__u_core__DOT__u_intpipe__DOT__wb_reg_xcpt`; generate-loop
replicas are `__BRA__N__KET__` on the Verilator side and an `__iN` suffix on the
lhd side. 936 of 1,075 core flops matched by name that way.

**`--probe` truncates every signal wider than 64 bits** (issue 2) — use
`observe_signals` for those.

---

## 4. Issues

### 4.1 — `--probe` silently truncates >64-bit signals

`probe_signals(const std::string&, std::map<std::string, long>&)` — every value
is a `long`. A 262-bit register comes back as its low 64 bits with no warning.
This produced a confident but wrong intermediate diagnosis (`f6_resp_q` looked
like it had lost its top 192 bits when it was byte-identical to Verilator).
`observe_signals` uses strings and is correct. Either widen the probe path or
make it refuse >64b rather than truncate.

### 4.2 — Pyrope path refuses at setup

```
conditional instance `sub_2856:txfma_trans_top` in `vpu_lane.vpu_lane`
has state whose clock cone is not reducible to a declared input
  hint: activation gating can only withhold a clock that reaches the callee
        through one of its input ports
```

From the uncommitted `if` condition in `minion/pyrope/vpu_lane.prp`. Goal 2 is
blocked until that conditional instance takes its gated clock through a port, or
the tree is regenerated. (Earlier in the same session the Pyrope path *did*
build and agreed with Verilog exactly — so this is a recent edit, not a
long-standing gap.)

### 4.3 — Latch windows collapse on a non-reference clock port

`cgen_sim.cpp` (~line 6157) models latch transparency by emitting next-state
twice, forcing `ref_clock_pin` to 0 then 1. It forces **only** the pin
`clock_input_of()` chose. When the latch is gated on a *different* clock port,
neither window substitutes anything and both come out as the same expression:

```cpp
cg_59 = (__in.preview_clk_i == 0);   // "low" window
cg_61 = (__in.preview_clk_i == 0);   // "high" window — IDENTICAL
```

The hold leg is unreachable; the latch is transparent for the whole period.
This remains independently reproducible in the fixture below.

### 4.4 — `cycles:u20` does not clamp

`--cycles 20000000` on a `cycles:u20` parameter runs all 20M and prints
`ran 20000000 cycles`. Convenient for benchmarking, but an out-of-range test
parameter accepted silently is probably a missing diagnostic.

---

## 5. Pending regression fixtures

Both auto-register via the existing `glob(["tests/sim/*.prp"])`.

| file | pins | status |
| --- | --- | --- |
| `inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp` | a design with NO register on the implicit reference clock gets no edge detection at all — `posclk=false` flops commit every tick | **FAILS** (8 asserts) |
| `inou/prp/tests/sim/latch_sim_nonref_clock_window.prp` | 4.4, in 20 lines: two clock ports, latch gated on the non-reference one, both windows collapse | passes — reproduces the *emission*, not yet a wrong value |

The second one currently passes and says so in its header; do not read a green
run there as evidence 4.3 is gone. Check it by grepping the emitted child:

```bash
$L sim inou/prp/tests/sim/latch_sim_nonref_clock_window.prp --setup-only --workdir /tmp/w
grep -n 'cg_1 =\|cg_2 =' /tmp/w/sim/*prevrf.cpp     # identical today
```

Baseline for the fixture suite: **70 of 74 pass**. `gate_on_named_clock_port`
and `test_args` fail before any of this work — pre-existing, not regressions.
(`bazel test //inou/prp:...` is the normal route; running each `.prp` through
`$L sim` directly works too and was used here.)

---

## 6. Next steps

1. Unblock the direct Pyrope design path and compare it against the now-passing
   Verilog/Verilator trace.
2. Remeasure setup, host compilation, and steady-state cycles/s after the
   correctness fixes; profile before changing the generated simulator.
3. Fix wide `--probe` handling so debugging cannot silently truncate state.
4. Resolve the independent non-reference latch-window and test-parameter-width
   issues, keeping their live regressions.
