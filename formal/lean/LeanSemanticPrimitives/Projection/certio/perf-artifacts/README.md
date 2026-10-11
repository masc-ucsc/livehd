# Raw profiler artifacts

Everything in this directory was produced by the commands below.  Nothing here
is derived by hand.

## Provenance

    repository revision   8bc115a73b2c5a48512f0f47643b68a5da7fc386
    lean-toolchain        leanprover/lean4:v4.31.0      (formal/lean/lean-toolchain)
    toolchain             Lean (version 4.31.0, x86_64-unknown-linux-gnu, commit 68218e876d2a38b1985b8590fff244a83c321783, Release)
    leanc                 clang version 22.1.4 (https://github.com/llvm/llvm-project 35990504507d79e0b9deb809c8ee5e1b34ceef20)
    valgrind              valgrind-3.27.1
    perf                  perf version 7.1.5
    host                  Linux 7.1.5+kali-amd64 x86_64

TOOLCHAIN TRAP, worth stating because it silently bites: `lean` is resolved by
elan PER DIRECTORY.  `formal/lean/lean-toolchain` pins v4.31.0, so every number
here was produced by v4.31.0 -- but running the same command from the
REPOSITORY ROOT gets elan's `stable` default, which on this machine is v4.34.1.
Run everything from `formal/lean`, as `build-native.sh` does (it `cd`s to its
own parent first).

The probe binary was built by `formal/lean/scripts/build-native.sh`, which
runs, per module, in dependency order:

    lean  -o <olean> -c <module>.c  <module>.lean
    leanc -c -o <module>.o <module>.c
    leanc -o perf_probe <probe>.o <module>.o ...

No extra optimisation flags are passed; `leanc` supplies its own defaults.
Use `build-native.sh --clean` for any before/after number -- incremental mode
rebuilds every module after the first dirty one, but a wiped tree is the only
thing that rules out a stale artifact entirely.

NOTE: these artifacts were taken with the probe as of 8bc115a73, whose `one`
called `Hw.mkSim` exactly once.  The probe has since gained per-stage
reporting (`projectDesign` / `checkResidual` / bundle) so that a failure says
WHICH stage failed; that change does not alter the specialization work these
files measure.

## Commands

    # build
    bash formal/lean/scripts/build-native.sh --clean

    # wall time / residual size / bound / RSS
    ./formal/lean/.native-dev/perf_probe 64 128 256 512 1024

    # exact call counts and instruction costs  (cg.N -> callgrind.chainD-N.out.gz)
    valgrind --tool=callgrind --callgrind-out-file=cg.N --quiet \
      ./formal/lean/.native-dev/perf_probe N

    callgrind_annotate --threshold=90 cg.N                  # self cost
    callgrind_annotate --inclusive=yes --threshold=99.99 cg.N   # inclusive

    # sampled self time
    perf record -q -g --call-graph=dwarf,4096 -F 199 -o perf.data \
      ./formal/lean/.native-dev/perf_probe 256
    perf report -i perf.data --stdio --no-children -q --percent-limit 0.3

## Files

    callgrind.chainD-{32,64,128}.out.gz   raw callgrind output, gzipped
    callgrind.chainD-{32,64,128}.self.txt callgrind_annotate, self cost
    callgrind.chainD-{32,64,128}.incl.txt callgrind_annotate, inclusive cost
    perf.chainD-256.self.txt              perf report, self time

## Reading the inclusive files

INCLUSIVE COSTS OVERLAP AND MUST NOT BE SUMMED.  `prepare` at 39.90% and
`PVal.shift` at 27.11% share the instructions `PVal.shift` executes when
called from `prepare`; adding them double-counts.  The self-cost files are the
ones whose numbers are disjoint and therefore addable.

Inclusive figures for SELF-RECURSIVE workers (the `'2` symbols) are inflated
past 100% by callgrind's recursion handling and mean nothing on their own --
`mixTerm'2` shows 25847%.  Only the non-recursive entry wrappers are read
above.
