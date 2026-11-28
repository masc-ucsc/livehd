# The `lhdcert-ckpt v1` checkpoint format

A checkpoint is the state of one design at one step: every flop, and every memory word that a
step can read. The Lean simulators share one format. The IR, VC, FP, TV and legacy routes write
the same bytes for the same state, and each route can continue from a checkpoint that any route
wrote.

The format, `save`, `restore` and every load-time check are in
`formal/lean/LeanSemanticPrimitives/Compiler/Ckpt.lean`. The driver is
`pass/lean/tools/sim/lhd_ckpt.lean`.

## The file

ASCII lines, then a digest:

```
lhdcert-ckpt v1
design <16 hex>          fnv1a64 of the certificate's DCERT4 rendering (`renderCert`)
cycle <decimal>          steps applied since power-on
flops <n>
<width> <hex>            n lines, by flop ordinal
mems <m>
mem <aw> <dw> <depth> <default hex> <k>
<addr hex> <value hex>   k lines
exts <e>
ext <name bytes> <name> <payload bytes>
<payload>                taken by its byte count, then a newline
digest <16 hex>          fnv1a64 of every byte before this line
```

- Flops and memories are listed by their ordinal in the certificate. Names are not stored.
- Values are unsigned, at the declared width. Hex digits are lower case, without `0x`.
- A memory lists only its words below `depth` that differ from its default. Addresses are
  strictly increasing. Every other address reads the default: the addresses at or past `depth`,
  and the negative ones. No step reads or writes those (`Projection/MemAddrSound`), so the
  default there has no effect on any run.
- The design hash is taken over `renderCert D`, not over the file. A DCERT3 file converted by
  `dcert_upgrade` is the same record, so a checkpoint taken against the DCERT3 file checks against
  the DCERT4 one.
- An `ext` section is opaque. Any tool that does not know its name copies it unchanged. A run
  loop can keep its host state there.

Example: the power-on state of `ckpt_selftest.sh`'s `pw` design, an 8-bit counter with power-on
value 5 and a 12-word memory loaded by `$readmemh` (`lhd_ckpt init`).

```
lhdcert-ckpt v1
design fa7a40633141179b
cycle 0
flops 1
8 5
mems 1
mem 4 8 12 0 9
0 3a
1 c5
3 ff
4 10
5 22
6 34
7 46
a 5d
b 6f
exts 0
digest e06f00cd7715b001
```

## Load-time checks

`validate` refuses a checkpoint that does not fit the certificate. It reports the first rule
that fails. Every message starts with `ckpt:`.

| rule | message |
|---|---|
| design hash | `design hash … does not match the certificate's … (fnv1a64 of its DCERT4 rendering)` |
| flop count | `<n> flops, the certificate declares <m>` |
| flop width | `flop k: width w, the certificate declares w'` |
| flop value | `flop k: value 0x… does not fit in w bits` |
| memory count | `<n> memories, the certificate declares <m>` |
| memory shape | `memory k: aw=… dw=… depth=…, the certificate declares aw=… dw=… depth=…` |
| default | `memory k: default 0x… does not fit in dw bits` |
| a word past depth | `memory k: word at 0x… is past the memory's depth d` |
| a word value | `memory k: word at 0x… value 0x… does not fit in dw bits` |
| order | `memory k: addresses not strictly increasing at 0x… (after 0x…)` |
| a repeated default | ``memory k: word at 0x… repeats the default (run `lhd_ckpt normalize`)`` |
| memory versions | `memory k's next image (slot s) is not a version of memory k; a checkpoint cannot determine this design's future` |
| word width | `node j stores w-bit words into memory k, whose words are dw bits; a checkpoint keeps dw bits` |

The last two rules are about the certificate, not the file. Without them the saved words do not
determine the next state.
- A memory's next image (`nextImg`), its commit images and its asynchronous-reset image must
  each be a version of that same memory: following writes and loads back from it reaches the
  memory's own `memImg` source (`MemAddr.rootOf`).
- Every word a step stores into a memory must have the memory's width `dw`: the data of a plain
  write, and the node width of a byte-enable write or a whole-array load. A read resizes the
  word to the read's own width, so a wider stored word would show bits the checkpoint dropped.

The parser refuses a file that is not `lhdcert-ckpt v1`, a truncated file, a bad number,
trailing bytes after the digest line, and a digest mismatch.

## Save and restore

- `restore c` builds a `RuntimeState`. Each memory becomes a dense table, built once.
- `save D hash cycle exts s` reads a state back. A memory's default is its word at address -1.
  The listed words are the words below `depth` that differ from it.

`Compiler/CkptSound.lean` proves, with no axiom beyond `propext`, `Classical.choice` and
`Quot.sound`:
- `save_restore` (T1): on a checkpoint that `validate` accepts, `restore` then `save` gives the
  same checkpoint.
- `validate_save` (T2): every checkpoint `save` writes passes `validate`, when the certificate
  passes the two certificate-only rules.
- `restore_save_flop`, `restore_save_mem` (T3): the restored state reads like the saved one,
  each flop at its declared width and each memory word below `depth` at the memory's width.

`Projection/CkptResume.lean` proves that this is all a step can see:
- `step_agree` (T4): two states that agree where a step can see them (each flop at its declared
  width, each memory word below `depth` by its unsigned value) give equal outputs and next
  states that agree again, on every input and clock vector. `trace_agree` extends it to every
  tick sequence.
- `resume_exact_reached`: for every state a run from power-on reaches, a run from its saved and
  restored checkpoint prints exactly the outputs the run itself prints, on every tick
  sequence. `resume_exact_from_ckpt` says the same for a run that started from a valid
  checkpoint, so a chain of resumes is exact too.

The hypotheses are the checks the loader and `validate` already run: the direct simulator
accepts the certificate (`checkDesign`), its memory addresses pass the load-time check
(`MemAddr.addrError`), and the two certificate rules of `validate` hold (`imgRootError`,
`wordError`). The second certificate rule is what makes a saved word all of the stored word:
every state a run reaches keeps each word at most `dw` bits wide (`wordsFit_trace`).

## `lhd_ckpt`

```
lhd_ckpt check     <cert> <ckpt>                   parse (digest) and validate against the certificate
lhd_ckpt diff      <a.ckpt> <b.ckpt>               the first difference, or SAME
lhd_ckpt seal      <in.ckpt> <out.ckpt>            recompute the digest of a hand-edited checkpoint
lhd_ckpt normalize <in.ckpt> <out.ckpt>            sort words, drop words equal to the default, reseal
lhd_ckpt init      <cert> <out.ckpt> [reset|zero]  the power-on state (default reset) at cycle 0
lhd_ckpt to-state  <cert> <ckpt> <out>             the same state as an lgraph-sim `--state` file
lhd_ckpt import-lhd <cert> <ckpt dir> <out.ckpt> ...  an `lhd sim` checkpoint, imported (below)
```

- `<cert>` is a DCERT4 file, admitted with `CertIO.readCertChecked` (every earlier version is
  refused; `dcert_upgrade` converts a DCERT3 file).
- Exit codes: 0 for OK or SAME; 1 for REFUSED or DIFFER; 2 for usage or I/O errors; 3 when
  the checkpoint does not parse.
- `init reset` starts every flop at its declared reset value and every memory at its power-on
  image (its `$readmemh` words, 0 elsewhere). `init zero` starts everything at 0.
- `diff` compares memories word by word below `depth`, after normalizing both sides.

To edit a checkpoint by hand: change the lines, run `lhd_ckpt seal`, then `lhd_ckpt check`.
Run `lhd_ckpt normalize` first when you added words out of order or words equal to the default.

## Importing an `lhd sim` checkpoint

`lhd sim` (LiveHD's native simulator) writes a checkpoint every `sim.checkpoint_every` cycles of
a run longer than `sim.checkpoint_min_secs`, so the C++ simulator can fast-forward a long run and
a Lean route continue from it:

```
lhd_ckpt import-lhd <cert> <ckpt dir> <out.ckpt> [inst:<name>] [map:<file>] [fill] [emit-map] [sim:<dir>]
```

It reads both layouts `lhd sim` has written:
- The current one (LiveHD since 2026-11-21): `<workdir>/ckpt/<test>/c<N>/` with `state.bin` (the
  DUT state, 64-bit little-endian words), `frame.bin` (the testbench's; not read) and `meta.json`
  (`cycle`, `words`). Where each value lies comes from `<workdir>/sim/drv.tb.json` (each test's
  instances: `var`, `stem`, and `base`, the instance's first word) and
  `<workdir>/sim/<stem>.dut.json` (each field's `name`, `kind` (2 a flop, 3 a memory), `bits`,
  `word` and `entries`). A value is `bits` bits from bit `(base + word) * 64`, low bits first.
  Memory entry `e` lies `e * stride` bits further: the stride is the entry's storage width (8,
  16, 32 bits or whole words), or `bits` for a memory of at most 128 bits whose width is not a
  storage width (packed). `sim:<dir>` names the `sim` folder when the checkpoint was moved (the
  folder above the checkpoint must keep the test's name). Neither layout carries a version, so
  every key named here must be there, `words` matching `state.bin`'s size: a missing key is a
  layout this reader does not know, refused (exit 2) with the file and the key named and
  nothing written, never read as 0.
- The earlier one: `<workdir>/ckpt/<test>/ckp<N>/` with `regs.json` (registers by name), one
  `<inst>.<memory>.hex` per memory (`$readmemh` words), and `meta.json` (the cycle). `lhd sim` has
  not written it since 2026-11-21. `pass/lean/xpatch/transport.py` still writes it: xpatch builds
  a design's state from another design's run, by flop and memory name, and imports it here. So
  the reader stays.

- Names come from the `_io.json` beside the certificate. Flop `n` is the key `<inst>.<n>`: the
  field `n` of instance `<inst>` (earlier layout: the `regs.json` key). Memory `k` is
  `<inst>.<i>` (the field; earlier layout: the file `<inst>.<i>.hex`), `i` being the sidecar's
  `inst` for it (the cell's instance name), else its `name`. These are the names
  `lhd sim --list-signals` prints.
- `<inst>` is the testbench's instance name (`var`). It is found when the test has one instance
  (earlier layout: from the `<inst>.__in.*` keys) unless `inst:` gives it.
- `map:<file>` overrides names, one line each: `flop <ordinal> <key>` or `mem <ordinal> <key>`
  (earlier layout: a file name). `emit-map` prints the mapping used, in that format.
- A flop or memory with no match is refused, by name. With `fill` it takes its power-on value,
  and a note says so. Flop keys that no flop claims are listed in a note (earlier layout: inputs
  `__in.*` and clock history `__clkprev*` aside). A field whose width is not the certificate's
  is refused.
- Values are hex (`0x...`) or decimal; a negative one is taken modulo `2 ^ width`. A memory word
  at or past the memory's depth must be 0.
- The result is validated like any other checkpoint before it is written.

`lhd sim`'s design hash is not the certificate's, so the match is by names and shapes. This
version handles a flat design: registers of sub-instances need `map:` lines.
`tests/lhd_ckpt_import_test.sh` runs a design under `lhd sim`, imports every checkpoint in both
layouts (the earlier one written from the current one by a converter in the test) to
byte-identical checkpoints, and checks that a Lean run from power-on reaches the first one and
that a Lean run of 10 cycles from each reaches the next.

## Running from a checkpoint

Both trace drivers read and write checkpoints. Cycle numbers are absolute: a run from a
checkpoint at cycle `c` starts at step `c`, for the stimulus's `at` lines, the edge schedule and
the printed `STEP` lines alike. So its lines equal the same lines of a run from power-on.

```
cert_trace <cert> <cycles> <stim> [route:ir|route:vc] ... [ckpt-in:<file>] [ckpt-at:<k>:<file>] [ckpt-out:<file>]
dcert2_memroute <cert> <stepFuel> <workFuel> <cycles> ckpt:<file> ... [ckpt-at:<k>:<file>] [ckpt-out:<file>]
<Name>TvSim <cycles> <stim> ... [ckpt-in:<file>] [ckpt-at:<k>:<file>] [ckpt-out:<file>]
<Top>_LegacyTrace <cert> <cycles> <stim> ... [ckpt-in:<file>] [ckpt-at:<k>:<file>] [ckpt-out:<file>]
```

- `ckpt-in:<file>` (`cert_trace`) and the start `ckpt:<file>` (`dcert2_memroute`, the FP route)
  start from a checkpoint. It is validated first; `cert_trace` exits 1 on a refusal and
  `dcert2_memroute` exits 2. A run from a checkpoint runs `<cycles>` steps, from `c` to
  `c + cycles - 1`.
- `ckpt-at:<k>:<file>` writes the state reached after `k` steps, for `c <= k <= c + cycles`.
  It can be given more than once.
- `ckpt-out:<file>` writes the final state, at cycle `c + cycles`.
- A driver that advances the state does not read `ext` sections and does not write them back,
  because they describe the state at the old cycle. It prints a note when it skips them.

The FP route keeps each memory as a table of `2 ^ aw` words. `Projection/CkptStateM.lean` builds
that table from a checkpoint (`toStateM`) and proves it denotes `restore c` (`toStateM_denote`,
T6). The FP route saves through the state it denotes (`saveM`), so it writes the IR route's
bytes.

The TV and legacy routes are compiled per design, so each has its own driver, with
`cert_trace`'s arguments and lines (`HDLProof/Run/Trace.lean`, `traceRun`):

- TV: `pass/lean/tools/scripts/make_tv_sim.py <cert> --out <dir> [--build <root>]` writes one
  file, `<Name>TvSim.lean`: the certificate, its step reified by `reify_design_chunked`
  (`Compiler/ReifyGen`, run while the file is compiled) and `traceMain`. The step has
  `interpretDesign`'s type, so checkpoints are taken of its state as it is, and it compacts as
  `cert_trace` does. Where `prove_reified_chunked` (`Compiler/ReifyProof`; `prove_reified` for
  the plain reifier) proves the step equal to
  `interpretDesign`, T4 and T5 make its resume exact; this driver does not run that proof.
- Legacy: `make_legacy_sim.py <Top>_Lgraph.lean --trace [--io <cert _io.json>] [--build <root>]`
  writes `<Top>_LegacyTrace.lean`, the model then its driver, which takes the certificate as its
  first argument. The model keeps its own record. A checkpoint converts it by the certificate's
  names (`_io.json`): flop `<name>` is field `st_<name>`, memory `<name>` is field `st_<name>`,
  a function of the address; a memory restored from a checkpoint is a table of its words below
  its depth, built once, and its default past them. The generator refuses a name the record
  lacks, a width that differs, and a state field no flop or memory names, because the
  checkpoint would not hold the whole state. A model with one clock domain has no quiet cycle
  (`edges:third` is refused). Its memories are closures that grow with every write, so it
  compacts as `cert_trace` does: the state is replaced by its restored checkpoint every 256
  cycles of a long run of a design with memories, or every `compact:<K>`. Legacy has no proof against `interpretDesign`: its checkpoints are
  tested (below).

Both are built with `build-opt.sh` and `EXTRA_MODULES` (`Compiler/ReifyGen` for TV,
`Translation/LegacyFast` for legacy), which adds those modules to that build only.

## Tests

`pass/lean/tools/tests/ckpt_selftest.sh` exports three small designs with lhd. It runs
`tests/ckpt_check.lean` (the library on a certificate built in Lean, including a resume after
four steps). It then checks every `lhd_ckpt` command, the digest, and one refusal per rule.
Last, on a design with a 12-word RAM and on one with two clocks, an asynchronous reset and a
ROM, it runs the IR, VC and FP routes. All three must print the same trace and write the same
checkpoint bytes. A run resumed from each route's checkpoint, on each route, must print the
full run's remaining lines and end in the same checkpoint. The same holds for TV and legacy,
built for each design: their traces and checkpoints equal IR's, TV resumes every route's
checkpoint, legacy resumes IR's and IR resumes theirs (the one-clock design is compared under
`edges:all`, the schedule its legacy model runs).
