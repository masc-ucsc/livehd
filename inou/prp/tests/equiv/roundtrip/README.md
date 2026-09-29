# Pyrope writer roundtrips

Each `.prp` automatically adds `//inou/prp:prp-roundtrip-<stem>` and joins
`//inou/prp:integration`. Run one directly from the repository root with:

```
python3 inou/prp/tests/integration.py roundtrip inou/prp/tests/equiv/roundtrip/kw_bundle_keywords.prp
```

The shared runner emits Pyrope, recompiles the emitted units into graphs, and
requires LEC equivalence with the source. The internal proof budget is 20
seconds, with a 40-second process watchdog. No timeout or unsupported result
counts as success. Optional `// :top: entity` selects a top different from the
filename stem.

Keep behavioral regressions here, not assertions about a particular temporary
name or emitted spelling. Verilog writer inputs belong in `inou/slang/tests/sv/`
with `// :test: roundtrip`.
