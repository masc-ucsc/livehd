# Master-emitted D3 target manifests

These are NOT the canonical manifests. The canonical ones are one directory up
and they stay exactly as they are.

## Why a second set exists

`../coreet_122.tsv` and `../cva6_30.tsv` pin the IMPORTED corpora recorded in
`../PROVENANCE.tsv`: 85 CORE-ET and 46 CVA6 certificates emitted on
`b1-b2-verified-compiler` at `b04288cca51f79fc663ca2e49c54e1fae6e7f221`, before
the D2 clock work. Every one of those 131 files ends in the pre-D2 trailer

```lean
def X_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun X_designCert
```

and master's `compileAndRun` is now `DesignCert → ClockEdges → RuntimeInput →
RuntimeState → RuntimeResult`. The certificate does not typecheck here. The
`DesignCert` literal itself is fine — `clocks` defaults, which is why `cert` and
`compile` pass and only `typecheck` refuses — so the failure is confined to the
emitter's convenience trailer, not to the schema.

Their own provenance said this would happen: *"IMPORTED: compatibility evidence
only. Passing a gate here does NOT show this branch can generate the certificate
end to end."* The compatibility has now ended. A run against them is still worth
having, because it is the measurement that establishes the boundary, but no D3
target can be credited from it.

## What these manifests pin

The certificates THIS worktree emitted, collected by
`pass/lean/scripts/d3_master_corpus.py` from the D4 emission runs under
`generated/master_port/`. A certificate is collected only when both hold:

* its block's row in the emission run's `rows.tsv` says `emit_status=EMITTED` —
  a D4 run writes a certificate for blocks it then rejects, so the file system
  alone would have admitted 4 CORE-ET and 4 CVA6 certificates their own run
  refused; and
* the emitted trailer is post-D2.

A target without such a certificate still occupies a row, one that cannot be
credited, which is the whole point of writing the manifest at the denominator.

## What is unchanged

`cva6_30_frozen.tsv` here is a byte-for-byte copy of the canonical freeze file.
The canonical 30 target NAMES are read, never recomputed: re-selecting "the 30
largest blocks that currently have a certificate" against a smaller corpus would
quietly swap the hard targets out and report a better number for it. Nine of the
thirty — including both `cva6_hpdcache_*` giants and `csr_regfile` — have no
certificate in this corpus, and they stay on the list saying so.

## Regenerating

```sh
python3 -I pass/lean/scripts/d3_master_corpus.py \
  --from generated/master_port/d4_coreet_122 --out generated/master_port/d3_corpus/master_coreet
python3 -I pass/lean/scripts/d3_master_corpus.py \
  --from generated/master_port/d4_cva6_30   --out generated/master_port/d3_corpus/master_cva6
python3 -I pass/lean/scripts/d3_manifest.py \
  --coreet-certs generated/master_port/d3_corpus/master_coreet/lean \
  --cva6-certs   generated/master_port/d3_corpus/master_cva6/lean \
  --out pass/lean/manifests/master
```

`d3_manifest.py` run with its defaults still regenerates the canonical manifests
byte-identically; that is checked, not assumed.
