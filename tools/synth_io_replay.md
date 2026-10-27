# Replay whole-color synthesis with external timing

`bazel build -c opt //tools:synth_io_replay` builds a diagnostic driver for the
production ABC `Io_mapper`. It operates on a private copy of a mapped LGraph
library, without running placement. This allows a whole-design OpenSTA comparison
to include the resynthesis normally used during timing-driven placement.

Run `bazel-bin/tools/synth_io_replay config.json` with:

```json
{
  "lg": "/absolute/path/to/mapped/lg",
  "top": "top_module",
  "liberty": "/absolute/path/to/merged.lib",
  "output": "/absolute/path/to/fresh/result",
  "period_ps": 100,
  "measurements": "/absolute/path/to/boundary_times.json"
}
```

Omit `measurements` for an inspection run. `report.json` lists each synthesis
region and its boundary port bindings. `final_regions` contains the bindings
**after** remapping/refinement; use those for the next timing measurement.
Each run writes its own `lg/` and structural `verilog/` directories. The output
directory must not exist; the source library is not overwritten.

The measurement JSON maps region module names to input arrival and output
required times in picoseconds. Use the exact bit-level port names from the
bindings:

```json
{
  "color_module": {
    "inputs": {"a[0]": 30},
    "required": {"z[0]": 70},
    "used": {"inputs": {"a[0]": 10}, "required": {"z[0]": 90}}
  }
}
```

`used` is optional and describes the environment used by the previous mapping.
For shared module definitions, aggregate the latest arrival and earliest required
time across their instances. Exclude clock/reset paths in the external STA
constraints and omit unconstrained boundaries. The driver does not infer these
exceptions. `mapper_options` can supply additional ABC option string values.

This exercises the production proof-backed remapper and refinement, with a 5%
whole-color trigger. It does not reproduce placement parasitics or restore the
physical stage's per-region timing-budget history. Score the complete output
netlist again with the same libraries, clocks, loads, and exceptions: acceptance
by a local region objective does not guarantee improved whole-design WNS or TNS.
Keep the baseline and every round so a whole-design regression can be rejected.
