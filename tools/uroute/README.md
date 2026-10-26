# URoute offline tools and native interchange

Implementation of [3u-uroute](../../todo/livehd/3u-uroute.html) is in progress.
The native feature generator, strict map/weight readers, inference engine and
calibration are implemented. LiveHD exposes the three-rung final-GP ladder,
selectable bounded probes, immutable project-pool loading, deterministic support
selection, cache/provenance fields and final/probe feature exports. H2D and rigid
macro measurements expand physical bodies and pins before learned inference.
The offline model/exporter, atomic sample/weight publisher, episodic trainer and
map-scoring functions, routed-DEF ingestion, native baseline scoring and
calibrated dataset evaluation are implemented. Two frozen LFSR routed arms have been ingested and checked against
OpenROAD ODB wire lengths; this is partial U0 coverage. The local verified
corpus now has 15 routed samples across seven designs, including two distinct
I/O variants each of an adder tree, word selector and signed/unsigned shifter.
All additions retain independent wire-length checks and native/routed feature
gaps. Older nine-sample numbers below refer to their frozen input snapshots.
The new seven-design unseen-fold evaluation uses a separate fifteen-sample
snapshot and private weights; original CNN/Eyeriss evaluation remains required.

Still required: the remaining router designs/arms and feature-gap measurements,
original-design leave-one-design-out evaluation and U6
in-loop CNN/Eyeriss comparison,
U7 trained weights/provenance. The published DREAMPlace pin passed the complete
post-pull optimized regression: 3,777 tests fresh, maximum 15.5 seconds. The bundled manifest explicitly records training as pending; it contains
no weights or support samples. Synthetic test weights are never pretrained
weights. The existing `route.model=rsmt` default remains in effect.

Native code belongs to `../DREAMPlace/dreamplace/native/`. During development:

```
bazel test -c opt --test_timeout=30 --local_test_jobs=1 \
  //tools/uroute:all \
  @dreamplace//dreamplace/native:rudy_test @dreamplace//dreamplace/native:rudy_rsmt_test
```

The native implementation is published in DREAMPlace commit
`1b0cd1972f9968b49b8b139af5282d3b99fee68a`, now consumed by `MODULE.bazel`.
The complete pinned-dependency regression passes all 3,777 tests fresh, maximum
15.5 seconds, without an override. A local repository override is only needed
for further sibling co-development. No Python, Torch, BLAS or new
Bazel dependency is added to native inference.

Two further optimizations are uncommitted in the owning DREAMPlace
`rudy_uroute.cpp`; the published pin above does not contain them. For multiple
supports, matching convolutions reuse the exact query-channel prefix before
resuming the original support-channel f32 sequence. Interior input tiles use a
fixed-size copy without redundant zero filling, retaining the boundary path.
All five unchanged goldens, signed-zero and ASan/UBSan checks pass; all 27
held-out queries retain all 3,538,944 calibrated values exactly. The isolated
consumer build passes 37 fresh tests (maximum 2.1 s). In three alternating
rounds on retained CNN/Eyeriss frames, median complete enhanced calls improve
from 1.301/1.397 s to 0.995/1.068 s with unchanged output maps. These private
cross-design supports bypass original-design selection and have constant DRC
labels, so this establishes runtime only. Eyeriss still misses one second; no
general latency, original-design quality or in-loop improvement is claimed.
Evidence and retained binaries are under `repros/uroute_weight_lane_probe_20261025/`
and `repros/uroute_spatial_mean_probe_20261025/`. No commit or push was performed.

## Feature version 1

Coordinates use the caller's physical units. Arrays are **channel, x, y**, with
y contiguous, matching the placer's x-major route bins. Compute on the route
bins, area-resample to 256x256, then min-max normalize each channel independently.
Normalization extrema are retained as binary64 values. Constant maps normalize
to zero, including constant nonzero density/labels. Output values are binary32.

1. RUDY integrates `(1 / bbox_width + 1 / bbox_height)` over bin overlap,
   divided by bin area. Neither timing weights nor DREAMPlace pin-count weights
   apply. There is no obstacle path or capacity scaling in this geometric input.
2. PinRUDY adds that same demand once for each pin to its tile. Pins on the upper
   core boundary belong to the final tile; outside pins contribute no PinRUDY.
   Their full bounding box still participates in RUDY normalization. Zero-area
   net boxes contribute zero to both channels, consistent with legacy bbox RUDY.
3. MacroRegion is one for route tiles with positive overlap with a physical
   macro body, zero otherwise. Multiple macros form a Boolean union. Resampling
   this binary source grid may produce fractional values.
4. CellDensity sums standard-cell overlap area. Macros and zero-area IOs add no
   cell area. Placement masks, temporary fillers and halo/inflated envelopes are
   not physical cells. The caller supplies the physical design snapshot; native
   halo padding is removed if present. Cell inflation must already be undone by
   the placement snapshot callback.

Enabled nets only, in CSR order. Node order controls density accumulation.
Binning clips overlap to the core and never clips the net's normalization box.
Area resampling uses integer overlap numerators on coincident grids. Native
binary64 multiply/add operations round separately to match Python; this is
covered byte-for-byte, including metadata, on both square and rectangular grids.
`features.py` is the independent Python reference; it needs only the standard
library. Routed ingestion must use the exported route grid and coordinate units.

Routed DEF analysis uses the read-only physical snapshot reader. It retains
placement and routing BLOCKAGES, including shapes and attributes, in the
geometry provenance. These constraints do not replace physical macro bodies in
MacroRegion or change the four feature definitions. The placement reader remains
strict until row-segment support exists; the placement writer rejects snapshots
with blockages before opening the output so constraints cannot be discarded.

## Architecture version 1

This is a versioned implementation of the paper's figure and equations, not a
claim of compatibility with author-supplied checkpoints. The initial search did
not identify a public author code/weight release; that search does not establish
that none exists. The paper leaves block depths and pooling details unspecified.
Version 1 fixes them as follows; changing these requires a coordinated version
bump in Python, C++, the manifest and the test reference.

- Four scales: full resolution, floor(size/2), floor(size/4), floor(size/8).
  Every level has 64 feature channels.
- Query embedding: 3x3 convolution, 4 to 64 channels, followed by ReLU. Support
  embedding: the same with 5 input channels (features plus one normalized label).
- Each encoder level: two zero-padded 3x3 convolutions with a residual identity;
  ReLU after conv1 and after adding conv2 to the identity. Downsampling is
  bilinear, half-pixel coordinates, `align_corners=false`.
- Cross-matching independently at each level: concatenate query and support;
  3x3 128-to-64 convolution and ReLU; average over channels; 7x7 1-to-1 spatial
  convolution and sigmoid. Multiply the support embedding by that spatial gate
  and **sum**, without division, over shots. A 1x1 64-to-64 convolution and ReLU
  project the result. Figure 4 specifies the 7x7 gate.
- Decoder: concatenate the deepest match with the deepest query, then at each
  shallower level concatenate the match with the upsampled decoder result.
  Each fusion uses the same two-convolution residual block, 128-to-64, with a
  learned 1x1 shortcut. Final 1x1 64-to-1 convolution and ReLU.
- All convolutions have biases. No batch normalization, dropout or training-only
  state. The same weights evaluate utilization and DRC as separate one-channel
  tasks, each with the corresponding support labels.

`UrouteContext` holds immutable weights and 1..16 selected supports. It encodes
each nonconstant metric's support pyramid once at stage setup, through
`UrouteNetwork`, and reuses it across GP samples. Query tensors and matching/
decoder scratch stay local to each inference call; no query result is cached.
At 256x256 the retained pyramid is 21.25 MiB per support per nonconstant metric
(106.25 MiB for five supports). Constant calibration skips that metric's encoder
entirely. The architecture and file formats are unchanged. Raw
support-label minima and maxima are each aggregated by median (mean of the two
middle values for even shot counts). Calibration is linear:
`physical = median_min + prediction * (median_max - median_min)`. Predictions
may extrapolate above the support maximum; clipping them would suppress learned
overflow when routed support peaks below one. A constant calibration range fixes
the physical answer directly and avoids an unnecessary network pass. Utilization
is area-resampled to the route bins.
Directional output preserves RSMT's ratio **and** `max(h,v)=prediction`: scale
both by prediction/max(h,v), or set both to prediction when RSMT is zero.
RSMT pin utilization, blocked pins, unreachable nets, physical blockage and
detour measurements survive unchanged. Overflow is mean excess over one plus
those physical penalties. DRC is report-only at level zero; a positive calibrated
level adds mean excess DRC density above that level. This level is not calibrated
yet and must remain zero by default.

## File formats

All fields are little-endian. Readers bound sizes and reject unknown versions,
shape mismatches, nonfinite data, duplicate names, truncation and trailing bytes.

`features.bin` / `labels.bin` (`UROUTEM1`):

- 8-byte magic, then five u32 fields: format version=1, channels, x, y,
  normalized flag (0 or 1).
- Per-channel min/max pairs, each two f64 values.
- Contiguous channel/x/y f32 payload, exactly channels*x*y entries.

Features have four normalized channels. Labels have three **raw** channels:
utilization, DRC density, detour. Direction/layer group maps are separate raw
maps. Model support inputs concatenate features and one normalized label; they
have five channels. Network-only golden outputs are raw maps so values above
one remain visible before physical support calibration.

`weights.bin` (`UROUTEW1`):

- 8-byte magic, u32 architecture version=1, u32 tensor count.
- Each descriptor: u32 ASCII-name byte count (1..128), name bytes padded to a
  multiple of four, u32 rank (1..4), rank u32 dimensions, u64 payload offset.
- Payloads in descriptor order, with each tensor and the file end aligned to
  64 bytes. Tensor storage is contiguous f32; convolutions use O,I,Kx,Ky order.
- Every required tensor name/shape is checked against architecture v1. Extra
  tensors are errors. The current model occupies 5,541,120 bytes.

Native weights use a read-only mapping. Writers must publish immutable,
content-named weight files before atomically replacing the manifest; they must
never truncate a mapped file in place. The native implementation currently
requires a little-endian POSIX host (the supported macOS/Linux environments).

## Validation and scoring

The ordinary Bazel tests need no PyTorch installation. `inference_golden.json`
contains PyTorch outputs for deterministic synthetic fixture weights/inputs;
`fixture.py` reconstructs their weights in the test's scratch directory. It
checks odd grid sizes, multiple shots, all four scales, deterministic repetition,
absolute calibration, physical RSMT term preservation, skip behavior and corrupt
weight rejection. Tolerance is `1e-7 + 1e-4*abs(reference)` per output tile.

For an independent PyTorch exporter round trip and fresh random-weight/native
comparison, use an isolated Python environment with `torch` and `numpy`:

```
python -m tools.uroute.golden bazel-bin/tools/uroute/native --production-grid
# Also compare cached/fresh forwards and time one/five-shot production queries:
python -m tools.uroute.golden bazel-bin/tools/uroute/native --benchmark-reuse
# Only when intentionally regenerating the checked reference:
python -m tools.uroute.golden bazel-bin/tools/uroute/native \
  --refresh tools/uroute/inference_golden.json
python -m tools.uroute.export_weights state_dict.pt weights.bin
```

`score.py` implements Pearson, average-tie Spearman, local Gaussian SSIM
(11x11, sigma 1.5, population moments, clamped boundaries), range-normalized RMSE,
and top-5% hotspot overlap. Undefined constant-map correlations/NRMS are JSON
null rather than misleading perfect scores. Hotspot ties use fractional cutoff
membership, so a flat map has chance overlap instead of an arbitrary index bias.
RUDY TSV comparison uses max(horizontal,vertical), excluding the separate
pin-access penalty because the reference is measured from wires.

```
python -m tools.uroute.score --reference labels.bin --prediction prediction.bin
python -m tools.uroute.score --reference labels.bin --prediction-tsv rsmt_enhanced.tsv
```

These tests establish implementation equivalence and arithmetic invariants.
They do not establish routing prediction quality. The router baseline, training
provenance, leave-one-design-out scores and in-loop CNN/Eyeriss runs remain
required before any default change or pretrained-model claim.

A local 256x256 CPU check on 2026-10-24, using synthetic seeded weights, compared
fresh versus reused support encodings in the same process. One-shot queries took
2.00 seconds after 0.57 seconds of setup, versus 2.58 seconds with fresh encoding;
five-shot queries took 4.01 seconds after 2.68 seconds of setup, versus 6.67
seconds with fresh encoding. Cached/fresh forwards match byte-for-byte across
changed queries and after the caller's support maps are released. Both production
shapes match PyTorch within 7.92e-7 relative error. The guarded Python/PyTorch plus
native benchmark took 43.73 seconds and peaked at 2228 MiB for its whole process
tree; that memory number includes both PyTorch models and is not native-only RSS.
Results are retained in `repros/uroute_validation_20261024/support_cache_benchmark.*`.
The context evaluates utilization and DRC separately; these are not combined
RSMT-plus-network or in-loop timings. The approximate one-second estimate remains
unmet. The 22 focused implementation targets pass with support reuse enabled.

The 2026-10-25 follow-up adds a complete-estimator timing path:

```
bazel-bin/tools/rudy_metrics physical.txt output_prefix uroute weights.bin \
  support_features.bin support_labels.bin [more_features.bin more_labels.bin ...]
```

The caller chooses 1..16 support pairs; this diagnostic helper does not select
from a project pool. Its JSON reports context setup separately from the base
and enhanced calls. Each call includes physical feature generation, RSMT/FLUTE,
neural prediction, absolute calibration and the retained physical penalties.
Existing `rudy_metrics physical.txt output_prefix [rudy|rsmt]` calls are unchanged.
On the recovered CNN frame (422944 enabled nets, 1167199 pins), enhanced RSMT
took 0.130 s. With synthetic fixture weights, complete URoute took 2.13 s for
one support and 2.66 s for two. A five-map performance workload explicitly
repeated those two real LFSR pairs: 4.24 s per call after 2.77 s setup, 397 MiB
native peak. These are not five independent routed shots or quality results.
Both DRC support maps are clean, so that branch uses constant calibration; the
neural work measured here is utilization only.

A portable four-output convolution block reuses input loads while preserving
each output's accumulation order. Complete-call maps match the prior kernel
byte-for-byte. In the same CNN workload the observed enhanced times became
1.99 s for one support and 3.95 s for five maps, with 2.60 s five-map setup.
Fresh production-grid predictions still match PyTorch within 7.92e-7 relative
error; all 22 focused targets pass. This is a modest local improvement measured
while routing was active, not a portable latency guarantee or a demonstrated
one-second budget. Results, binary hashes and source snapshots are under
`repros/uroute_route_recovery_20261024/combined_estimator_benchmark/`;
`conv4/` contains the kernel comparison. No benchmark weights enter a project
training pool or the bundled pretrained directory.

Processing eight spatial rows at a time keeps the four output tiles local to
the CPU cache while preserving each output's ic/kx/ky accumulation order. In
the same retained CNN workload, enhanced calls fell from 1.99 to 1.43 s for
one support and from 3.95 to 2.83 s for five map arguments; five-map setup was
1.84 s. Both output maps remain byte-identical to the previous implementation.
The fresh one/five-shot PyTorch check still has maximum relative error
7.91e-7, and all 24 focused targets pass. This synthetic-weight workload has
only two independent physical supports and one neural metric; the one-second
target remains unmet. Source snapshots, executable identity and comparisons
are in `combined_estimator_benchmark/tile8/`; fresh golden and focused-suite
logs are `repros/uroute_validation_20261024/tile8_{golden,full_focused}.*`.

## Placement options and data ownership

```
lhd synth design.v --workdir W --set synth.place=true \
  --set synth.place.route.model=auto --set synth.place.uroute.dir=/project/uroute
```

`route.model=uroute` selects learned feedback directly in final cell GP; missing
or incompatible data produces an explicit `uroute_skipped` reason and RSMT
feedback. `auto` first latches RUDY to RSMT, then requires consecutive RSMT
samples at/below `final.route.uroute_switch` (default .01) before latching URoute
(`final.route.uroute_hold=2`). A converged RSMT candidate waits for that hold
window when a usable learned context is available. Both overflows and switch
iterations are recorded. Every latch is one way and survives continuation.

FP/macro `route.probe=uroute` performs one convergence probe; failure enables
bounded feedback using the existing `probe_iterations` budget. `true` and
`rsmt` retain the existing RSMT probe choice. Earlier stages resolve a global
`route.model=uroute` to RSMT until an explicit learned probe is requested. H2D
probes expand its frozen macro abstraction; abstract search and macro-pressure
scoring stay on RSMT after a learned probe. Rigid macro groups are likewise
expanded before feature extraction. Probe exports retain the measured physical
geometry rather than group reservation rectangles.

The directory defaults to `@pretrained`, resolved through declared runfiles.
An empty `uroute.dir` disables both import and feature exports. `uroute.export=true` writes
`W/synth/place/uroute_export/features.bin` and `metadata.json`, including core,
route/network grids, track capacities, geometry identity, DBU scale, top,
technology and floorplan signature. Requested probe exports use `fp_probe/`,
`macro_probe/` or `final_probe/` underneath that directory. Final export rebuilds
physical pin geometry after legalization/repair, including cache-hit runs.
`uroute.export=false` disables these exports independently of inference.

Technology identity hashes the input LEFs and, for native memory proxies, the
exact generated `memory_proxies.lef` bytes after routing-track alignment. The
routed importer reads that emitted LEF too, so an unchanged next invocation
matches its own routed support without adding generated outputs to its inputs.

`uroute.shots=5` (1..16), `uroute.min_shots=1`, `uroute.fallback=nearest|skip`
and `uroute.drc_level=0` configure selection/calibration. Same top and technology
use newest-first samples with floorplan tie-breaking; below `min_shots`, nearest
normalized query features are used, preferring matching technology. Failed
routes and incompatible sample versions cannot enter the support set. The
manifest SHA-256, weight SHA-256, selected IDs and resolved controls enter cache
identity; histories record separate hashes and IDs for replay after pool growth.
Resolved values and skip reasons are in `route_options.json`, `gp_history.json`
and placement QoR. Cached placements report their routing model as null because
no fresh routing evaluation ran in that invocation; retained GP history describes
the earlier cached solve.

The offline `pool.py` publisher owns updates. Manifest version fields are all
1. `samples` is an array of records containing `id`, relative `directory`, `top`,
`technology`, integer `created_at`, `status`, the three versions, feature/label
SHA-256 values and a `floorplan` object (`die_width`, `die_height`, `utilization`,
`macro_signature`). `weights` is null or `{file, sha256}`; `trained_on` and
`provenance` retain the training identity. Sample directories contain immutable
`sample.json`, `features.bin`, `labels.bin` and optional sidecars. Failed routes
retain `status=unrouted` and `last_global_overflow.bin`. Publishing serializes
writers, installs immutable content-named objects, then atomically replaces the
manifest. Re-ingesting identical content preserves its first timestamp and ID
rather than creating duplicate support shots. Native readers map weights read-only and keep a manifest snapshot.
They reject duplicate fields/IDs, escaping paths, corrupt hashes, unsupported
versions and malformed maps with a clear skip reason.

## Offline training

In a user-managed environment with PyTorch and NumPy:

```
python -m tools.uroute.train --pool /project/uroute --holdout validation_top
# Deliberately initialize a new universal model when no weights exist:
python -m tools.uroute.train --pool /project/uroute --holdout validation_top --from-scratch
python -m tools.uroute.train_smoke
```

The trainer requires at least two gradient-training designs, a held-out
validation task, and at least two routed placements in every task. `--holdout`
uses every task of that design for early stopping. That design is part of model
selection, so it is not an unseen test design. Tasks are top/technology/metric;
query and support IDs are disjoint. Default support size is five, reduced when
a task has fewer earlier samples. Each episode uses paired rotation, flip and
zero-padded translation augmentation, MSE and AdamW. Sampling weights are
`sqrt(mean(last_10_task_losses)) + .01`; inverse-probability loss weighting
preserves the uniform-task objective. Held-out task loss controls early stopping.
This original-plan probability rule remains the default. Opt-in
`--sampling paper-rms` follows the paper's Eq. 15 and Algorithm 1 instead:
RMS of the last ten losses, normalized and mixed with 1% uniform probability.
These are different rules; an additive unnormalized .01 is not a 1% mixture.
Both the trainer and fold runner record the selected rule.

`--linear-head-loss` is a separate opt-in stability experiment: it computes
training MSE before the final nonnegative clamp, so negative preactivations
retain a restoring gradient. Validation and exported native/PyTorch inference
keep the architecture-v1 clamp and unchanged tensor layout. It defaults off.
The CPU smoke checks both the zero ReLU gradient at a forced negative head
and the nonzero pre-clamp-loss gradient, as well as normal training/inference.
The current weight export initializes fine-tuning unless `--from-scratch` is
explicitly requested. CPU is supported for small pools, CUDA when available.
`--device auto` retains its CUDA-if-available, otherwise CPU policy. Apple Metal
is an explicit option in both `train` and `cross_validate`:

```
python -m tools.uroute.train --pool /project/uroute --holdout validation_top \
  --device mps --mps-memory-limit-gib 4
```

Metal requires a finite positive allocator limit no larger than PyTorch's
recommended maximum. Zero is rejected because PyTorch treats it as unlimited.
The cap, recommended maximum and allocator fraction enter runtime provenance
and the run hash. This bounds the Metal allocator; CPU objects and other
processes still need host memory headroom. An unavailable explicitly requested
accelerator fails rather than falling back. Training never occurs in LiveHD.

A project training lock prevents overlapping trainers. Per-epoch losses,
importance weights and timing are retained in unique
`train/log_<run_hash>_<attempt>.jsonl` files;
`train/log.jsonl` contains the latest completed run. Publishing
records exact sample IDs/digests, initial weights, held-out design, options and
PyTorch version, while preserving samples concurrently added by ingestion.
The run hash also covers the actual CPU/CUDA/Metal runtime and hashes of the local
training implementation. Exact sources are retained under
`train/source_<implementation_hash>/`; a changed retained source is rejected.
Each attempt freezes its complete input manifest, including any initial model's
provenance, in a sibling `.input_manifest.json` file. Published provenance names
and hashes that snapshot and its log. Identical recipe reruns receive separate
attempt IDs, logs and intermediate exports, preserving earlier evidence even
when execution times or backend results differ.
The offline smoke uses temporary synthetic samples and validates both initial
training and fine-tuning on CPU. It establishes workflow correctness, not
prediction quality or pretrained-model provenance.


A bounded real GCD final-GP comparison is retained in
`repros/uroute_gcd_in_loop_20261025/`. Both legs completed actual detailed
routing (41.69/41.77 s), wire-length verification and displacement measurement,
but `comparison.json` explicitly reports `no-learned-exposure`: their 100 GP
iterations never crossed the existing density-overflow <0.3 sampling gate.
Identical 2933.556 um enabled-data wirelength and zero DRC therefore do not
measure a learned-model effect. A fresh 1500-iteration comparison completed
under `repros/uroute_gcd_in_loop_density_ready_20261025/`; the gate and defaults
are unchanged. It records 69 measured URoute samples (37 main GP, 32 timing
round), with the first main-GP learned switch at iteration 761. Both legs are
legal but GP-unconverged. Both produce byte-identical placed DEFs, zero DRC,
2514.922 um enabled-data wirelength, native pre-CTS WNS 248.680 ps/TNS 0, HPWL
2114.937 um and identical legalization displacement (69.757 um total standard-cell
movement, 0.206 um maximum). Routed nominal utilization has mean .34307 and
peak 1.82044. RSMT and learned estimator peaks stay below one (.596/.712), so
neither applies route inflation; this workload cannot demonstrate a learned
placement effect despite measured exposure. Synthesis/mapping/placement takes
82.09/173.63 s and detailed routing 27.96/28.18 s. The corrected pool admission
coalesces the identical routed pairs into one private sample plus one observation.
No model is promoted and these data do not calibrate the production switches. Both legs use the same immutable inference-pool snapshot and a
separate output pool; neither can consume labels generated by the other leg.
The private candidate saw GCD during training, so even a completed exposed run
would be workflow/within-design evidence, not an unseen CNN/Eyeriss result.

Original CNN/Eyeriss U6 inputs and paired settings are pinned under
`repros/uroute_original_in_loop_20261025/`. The refreshed runtime under
`repros/uroute_original_in_loop_current_20261025/` includes the numerical and
physical-cache corrections, its own 177 runfiles, rechecked physical-input
hashes and unchanged paired settings. Native OpenTimer preflights on private
retained mapped graphs complete for both designs and reproduce retained WNS/TNS
using their saved approximate placement-Flute RC. CNN takes 26.1 seconds at
10.5 GiB peak; Eyeriss takes 39.6 seconds at 13.8 GiB. Both reports retain
`constraints_complete=false`; CNN also has two native-state timing boundaries.
These are large retained-design preflights outside the default test suite,
not fresh mapping, placement/RC, full timing coverage or in-loop quality.
See the refreshed directory's `retained_sta_validation.json`. Model/support
bindings remain absent and both recipes remain non-runnable.

Initial graph/interface preparation follows. Current `lhd tool tree` reads
isolated copies of both saved elaborated graph inputs; complete before/after
hash checks show those copies unchanged. Retained library/LEF hashes and the
current typed-option vocabulary are checked. Corrected floorplans retain 365
adjustable CNN signal pins and 57 adjustable Eyeriss signal pins; no saved
placer `FIXED` signal status is promoted into a user constraint. The two arms
differ only in `synth.place.final.route.model`; FP/macro/global estimators are
held at RSMT, thresholds remain .05/.01 with holds 1/2, and track import is
explicit so fresh native proxy pins use actual routing tracks.

A narrow Tcl endpoint capture matches the retained clock, IO and reset
constraints over those signal interfaces. Eyeriss's pinned reference SDC also
retains `sta_dynamic_loop_breaking=1` as a separate external-STA policy. This is
not native OpenTimer/OpenSTA execution, graph-to-RTL equivalence, fresh mapping,
placement or routing validation. `design_recipes.json` explicitly marks the
recipes non-runnable until original held-out folds and a common immutable
model/support snapshot are bound. Same-top/technology support must then pass
preflight for the newly generated proxy geometry; legacy CNN support may have
a different LEF fingerprint. A skipped model, absent measured exposure, missing
route inflation, or unconverged GP cannot become a quality claim. The existing
large routes and follow-up I/O queues remain unchanged.

The first pair also exposed a duplicate-shot bug: its routed DEF, feature maps
and raw labels were byte-identical, but control-file provenance contained
different source-QoR hashes. Pool admission now records that as a new observation
of the first sample rather than another support shot. The full controls digest
remains in immutable sample/observation metadata; different effective feature
or label maps still create distinct samples. A replay of both real artifacts
produces one sample plus one observation, and the current canonical nine-sample
pool has no duplicates under the corrected identity. No old record or timestamp
is rewritten. The pool, ingestion, fold-readiness and evaluator targets pass.

## Routed extraction and ingestion

Build `//tools:placement_metrics`, `//tools:rudy_metrics` and
`//tools/uroute:native` with the same local repository override. Ingestion needs
no Torch or NumPy:

```
python -m tools.uroute.ingest --def 6_final.def.gz \
  --lef tech.lef --lef cells.lef --liberty cells.lib \
  --controls native_qor.json --drc 5_route_drc.rpt \
  --pool /project/uroute --source livehd --livehd-export W/synth/place/uroute_export
# Without an earlier LiveHD export, supply the target utilization explicitly:
python -m tools.uroute.ingest --def 6_final.def --lef tech.lef --lef cells.lef \
  --liberty cells.lib --controls controls.json --drc 5_route_drc.rpt \
  --pool /project/uroute --source external --utilization .55
python -m tools.uroute.baseline --sample /project/uroute/pool/SAMPLE_ID --output scores/SAMPLE_ID
```

`placement_metrics --uroute-geometry` reads physical components, rotated sizes,
pins, core rows and routing layers through LiveHD's existing LEF/DEF readers.
It reconstructs clock/reset classification using Liberty and `prects_controls`
roots; the feature, label and baseline net sets all exclude disabled controls.
The preexisting physical-HPWL snapshot retains its own clock-only exclusion
policy. Placement masks and LEF `CORE SPACER` fillers contribute no features.
The importer snapshots the DEF, recomputes routed features independently in
Python and C++, and requires byte-equal map files before publishing. LEF/DEF
units must agree. Liberty is a combined, uncompressed Liberty file. DEF input
can be gzip-compressed. ODB input is also supported through an explicitly supplied external OpenROAD
executable; native inference has no OpenROAD dependency.

An optional LiveHD export must have matching versions, digest, top, core and
DBU scale. Its route grid is reused, and its four normalized feature distances
and both raw ranges are retained. Without it, `--bins` defaults to 64. The
network/label grid is 256x256. This gap compares post-CTS/repair geometry against
the geometry the placer exported; it never substitutes the earlier features
for the routed sample's actual inputs.

For the U6 legalization-displacement column, the same scorer accepts a
pre-legalization reference DEF alongside the post-legalization `--def`:

```
bazel-bin/tools/placement_metrics --def W/synth/place/after_legalization.def \
  --reference-def W/synth/place/before_legalization.def \
  --lef tech.lef --lef cells.lef --lef memory_proxies.lef \
  --liberty combined.lib --controls W/synth/place/qor.json --output displacement.json
```

Collect both snapshots with the explicit `synth.place.snapshots=true` option.
The report separates standard cells, macros and physical-only instances, uses
origin Manhattan distance in microns, and counts orientation/status changes and
previously fixed-instance movement separately. It requires the same design,
units and complete instance/master census; it never treats a resized or partial
netlist as zero displacement. It excludes terminal movement. The existing HPWL
and geometry exports retain their behavior when no reference is supplied.
A real eight-instance memory-placement check measures seven moved standard
cells, 9.852 um total and 3.574 um maximum, with its fixed macro unchanged.
Four scorer/feature/ingestion/evaluation targets pass; evidence is under
`repros/uroute_in_loop_validation_20261025/`. This validates a required U6
measurement, not CNN/Eyeriss auto-versus-rsmt quality.

Label version 1 fixes the following extraction conventions:

- Track utilization is measured directly from merged, collinear regular-net
  centerlines on the 256x256 label grid. Per-layer stack groups are M2-M3,
  M4-M5 and M6 upward (positions in LEF routing order). Each group's horizontal
  and vertical wire length is divided by tile area times the sum of its
  preferred-direction inverse pitches. The single utilization label is the
  maximum of these six maps, also retained in `layer_groups.bin`. M1 local
  access is excluded from utilization, and included in detour length.
- Capacities are nominal LEF track densities, not discrete track counts or
  router-adjusted available capacity after obstruction/via reservation. No
  global-route congestion map is used as a wire label. Native baselines retain
  their own aggregate capacity over all routing layers, including M1; the
  baseline report states this capacity difference explicitly.
- DRC uses the OpenDB/TritonRoute text report. An empty file explicitly means
  no markers; a missing file is an error. Each marker counts once in the
  256x256 tile containing its bounding-box center. Out-of-core markers are
  counted separately in the report.
- Detour uses OpenDB-equivalent routed length (including rectangular patches
  and explicit point extensions) divided by native FLUTE length, with a cap of
  4096 pins per enabled net and no bbox fallback. The ratio is aggregated using
  centerline length as tile weight. Via enclosure metal has no length. Both
  the merged centerline length and OpenDB-equivalent length are retained per
  net in `extraction.json`.

The parser accepts OpenROAD's regular ROUTED/FIXED/COVER paths, split NEW paths,
wildcards, extended points, terminal vias and RECT patches. It rejects
non-Manhattan wires, non-default routing rules, unsupported path styles,
shield/subnet routes and points following a via without a NEW layer path.
Unsupported syntax on disabled clock/reset nets does not affect data labels.
Power SPECIALNETS are outside this signal-routing task. Route success is an
explicit caller assertion (`--status routed`); the extractor does not prove
connectivity or DRC closure.

For an independent length check, run OpenROAD on the matching final ODB:

```
report_wire_length -net {*} -detailed_route -file wirelength.txt
```

Pass that file as `--wirelength-report wirelength.txt`. Ingestion rejects
missing enabled nets or disagreement beyond the report's printed precision
and one DBU. It records the checked census and total lengths. Without this
option, no ODB verification is claimed. Failed routes use `--status unrouted
--last-global-overflow overflow.bin`: features and overflow are published,
labels are absent, and the sample is excluded from support/training.

The lower-level `python -m tools.uroute.extract` accepts `--geometry`, `--def`,
`--drc`, `--flute-lengths` and `--output` for inspecting extraction separately.
`native trees geometry.json flute.json` creates the per-net FLUTE denominators.
Ingestion publishes the geometry, physical summary, extraction report, lengths
and optional original export/report as immutable sample sidecars.

## Local evidence (2026-10-24)

The focused suite passed 20 targets, including ingestion/export, placement,
CLI and native routing tests; the longest recorded test was 7.7 seconds.
The live 256x256 PyTorch/native golden comparison had maximum relative error
5.42e-7 with synthetic weights. CPU scratch-training and current-weight
fine-tuning smoke both passed. These are focused implementation checks.

Frozen `route_compare_20261019` final LFSR DEF/ODB arms produced two real routed
samples with byte-equal Python/native features and immutable re-ingestion.
All 136 LiveHD-arm nets (213504 DBU) and 144 OpenROAD-arm nets (182133 DBU)
matched OpenROAD's independent detailed-wire report to floating-point precision.
Both DRC reports were empty. The legacy control roots were reconstructed from
known LFSR clock/reset ports and recorded as such; native control-census metadata
was absent. Full baseline metrics and source identities are in
`repros/uroute_validation_20261024/routed/summary.json` and the per-arm
`baseline.json` files. No learned weights were used in these measurements.

A separate gap measurement recomputed feature-version-1 maps from the frozen
LiveHD pre-CTS `out.def`: normalized RMSE was .07095 (RUDY), .02607 (PinRUDY),
0 (MacroRegion; this design has no macros), and .23160 (CellDensity).
`livehd_feature_gap.json` identifies this reconstruction explicitly; it is not a
contemporaneous export from the legacy run. CNN/Eyeriss, nonempty routed DRC
reports, leave-one-design-out prediction and in-loop router evaluation remain
unvalidated. The current frozen CNN/Eyeriss arms have no routed final DEF;
the two LiveHD arms fail detailed placement during CTS (DPL-0036). The retained
`route_inventory.json` records the available checkpoints. No failed or stopped
benchmark was resumed for these extraction checks. No complete U0-U7 milestone
is claimed.

Additional real pretraining coverage now includes the unchanged repository
36-bit GCD (`inou/yosys/tests/long_gcd.v`). The successful source placement used
one timing round, no I/O resynthesis, 100 GP iterations, a 20x20 um die and an
explicit 3 um core margin. It is legal but not GP-converged. The routing handoff
re-places I/O on M4/M5; before/after DEFs are retained. Optional post-CTS timing
repair is explicitly skipped after a separate attempt hit the hold-buffer limit;
this is not timing closure or a default-flow QoR comparison. Whole routed
geometry, rather than native placement features, supplies the training pair.

`repros/uroute_real_small_corpus_20261025/gcd36_margin3_ioplace_drt120/` completed
all routing/fill/final stages. Detailed routing took 43.57 s and peaked at
975 MiB, within the explicit 120 s/2 GiB data-generation guard. Other small
stages retain 30 s limits. The DRC report is empty. Ingestion verifies byte-equal
native/Python features and all 814 enabled-net wire lengths: both totals are
2933556 DBU, maximum per-net difference 9.1e-13 DBU. The native-export to
routed-feature normalized RMSE is .137887 RUDY, .042202 PinRUDY, 0 MacroRegion
and .338723 CellDensity, including handoff/CTS/IO changes.

On this layout, base bbox RUDY/RSMT Pearson scores are .62465/.60417 and NRMS
.27768/.27601. All six metrics and physical-capacity differences are retained
in its `baseline/baseline.json`; these are baseline measurements, not learned
prediction results. At the initial training checkpoint, the immutable verified pool had three designs and six
samples (two each of LFSR, GCD and the unchanged 128-bit IWLS adder). GCD's
second layout excludes top-edge I/O: before/after DEF checks confirm all 112
pins move and its top-edge pin count changes from 21 to zero. The adder's
native pins were fixed, so its explicit handoff releases signal pins before
replacement and asserts all 385 resulting shapes are on M4/M5. Both adder
routes have empty DRC reports; all 1027 enabled-net lengths match OpenDB to
floating-point precision. Its second layout also excludes top-edge I/O.
Each task now has two genuinely different feature/label pairs, sufficient
for an initial real-data training experiment. This supplemental corpus does not
replace the required CNN/Eyeriss routes or their in-loop comparisons. Rejected
inputs, library-assembly checks, CTS failures, failed boundary-I/O routes and
wall-time attempts remain separate and were not admitted to the pool.

Isolated real-data candidates train on GCD/adder with LFSR used only for
validation/model selection, excluding CNN and Eyeriss (`TOP`). They preserve the
canonical pool and are not installed in the bundled pretrained directory.
The first run completed 13 epochs in 43.82 s, with real-weight native/PyTorch
maximum relative error 1.30e-6 and byte-deterministic repeated native output.
Its one chronologically scoreable LFSR validation query has hotspot overlap
.0326; the paper-RMS candidate reaches .0623 and the pre-clamp-loss candidate
.0641, versus .1507 for base RSMT. Their NRMS is .2322/.2313/.2293 versus
.2335 for RSMT, illustrating why a small MSE
improvement alone does not establish a useful hotspot predictor. LFSR was used
for model selection, so these are not unseen-design scores. The earlier LFSR
query correctly skips for lack of earlier supports. Those initial six routed samples
are macro-free and clean-DRC; this remains limited training coverage. Models,
source/input snapshots and complete reports are retained under the corpus's
`pretraining_candidate_*` directories; no candidate is presented as ready U7
weights or a completed U6 comparison.
The longer isolated CPU candidate completed 54 epochs in 1028.24 s using
32 episodes per epoch and 25-epoch patience with both opt-in training modes.
Its LFSR hotspot overlap is .07355, still below RSMT. An explicit private Metal
experiment completed 58 epochs in 86.49 s with a 4 GiB Metal allocator limit,
a 6 GiB process-tree cap and the same split, seed and training recipe. These
are separate training trajectories with different stopping epochs, rather than
a controlled throughput benchmark. The public `auto` device policy is unchanged.
Metal's validation hotspot overlap is .07422 and NRMS .22996. Neither model is
promoted. The subsequently added public Metal CLI passes a separate one-epoch
train/export/native check and an excluded-design fold; allocator provenance is
verified. All 13 offline-tool targets pass, and the CPU smoke takes 17.53 s.
These checks are retained under `mps_cli_validation/validation_summary.json`. Their native predictions are deterministic and pass the existing
`1e-7 + 1e-4*abs(reference)` tolerance, with maximum absolute error 1.79e-7.
Maximum relative error near zero is .000753 (CPU) and .001394 (Metal), so these
checks do not establish the plan's strict uniform 1e-4 relative bound.
`real_training_comparison.json` retains the weights, provenance and full scores.

Two supplemental real-data folds now exclude GCD or adder from all gradients,
model selection and checkpoint ancestry. Each scores both routed queries with
verified unseen lineage in chronological and other-design-only protocols.
Chronological hotspot overlap is .11614 for GCD and .35651 for adder, versus
base RSMT .14812/.37899; NRMS is .27598/.16679 versus .27966/.19260.
Other-design-only overlap is .06609/.35450. Thus lower MSE does not establish
better hotspot prediction. All labels remain macro-free and clean-DRC; zero DRC
error does not validate prediction of violations. Full per-protocol metrics,
coverage and support selections are in `supplemental_unseen_summary.json` and
`supplemental_unseen_folds/run_kd09_269/cross_validation.json`. These completed
small-corpus folds do not replace original CNN/Eyeriss U6 or in-loop comparisons.


The original reference arms are audited separately in
`repros/uroute_route_recovery_20261024/reference_coverage_audit.json`.
`cnn_openroad_unrepaired/` is queued on the same global routing lock from the
preserved original OpenROAD `3_place.odb` and serialized SDC. It is a separate
timing-unclosed recovery: optional CTS and incremental timing repairs are skipped;
standard-cell legalization uses an explicit full-core search bound. All 65 macro
locations, orientations and fixed states must remain unchanged, and whole-design
legality is checked before and after CTS. The original placement and failed/stopped
attempts remain intact. Its driver hashes source inputs, technology, Tcl files
and executable before waiting, rejects changes on lock acquisition, and permits
no automatic retry. It runs the checkpointed pin-access/global/detail flow and
ingests only after final routing. It has not yet started CTS or supplied labels.
The original Eyeriss reference arm's SDC copy was deleted. Its committed
HighTide source remains available at commit
`e3953c139e78ad6c2d3962c833461ad3de97c3e1`. A bounded Tcl constraint capture over
all 59 reference DEF ports verifies that its 2500 ps clock, 500 ps input/output
delays and asynchronous-reset false path equal the retained LiveHD-written SDC.
`eyeriss_constraint_recovery/verification.json` retains hashes, captures and
port directions. Unknown ports/options fail; this is constraint equivalence,
not byte identity to the deleted file or an OpenSTA runtime proof.
The source additionally enables dynamic loop breaking, which serialized SDC
omits; the recovery's private `load.tcl` preserves that process state at each
fresh stage. `eyeriss_openroad_unrepaired/` is now queued from its saved
`3_import.odb`: it retains the existing routed power grid, bypassing the earlier
redundant PDN generation that hit a disk guard. Handoff and CTS must preserve all
1040 macro frames and pass whole-design legality before checkpointed routing.
Optional timing repair is skipped under the same explicit timing-unclosed
protocol. Input verification and no-retry behavior match the CNN reference queue.
It has not yet executed handoff/CTS or supplied labels.

A bounded memory-proxy coverage experiment exposed and then fixed a physical
export problem. The unchanged 16x2 memory initially lowers to flops; explicit
`synth.memory=macro` plus a small XOR wrapper yields a legal eight-cell placement
with one 3.361 um square proxy. The platform macro PDN grid fails because the
abstract proxy has no power pins. A distinct standard-cell-grid experiment then
passes CTS, pin access and global routing, but detailed routing fails at
`storage.rf/out_1[1]`: its M1 box spans x=8404..8422 DBU, while the next preferred
vertical track is x=8433. A read-only audit finds no planar same-layer power
overlap at that pin. Zero `macroNoAp` alone therefore did not establish routing.
These failed attempts and source hashes remain in `macro_coverage_investigation.json`.

The source correction uses preferred escape layers for the three pin edges,
places pin centers on the actual imported track patterns, and includes newly
packed proxies in routing-phase alignment before fixing them. Pin widths and
square body sizes are unchanged; explicit fixed origins remain immutable.
Tests cover interleaved patterns, distinct pins, fixed-origin rejection and a
large unrepresentable pitch without integer overflow. The corrected synthesis
uses the same platform `make_tracks.tcl` as the router, and its handoff preserves
those tracks instead of duplicating I/O slots. A final-source replay produces
byte-identical proxy LEF, DEF and feature maps to the successfully routed export.
All 25 focused native/LiveHD/tool targets pass; final placement tests pass again
following the last comment-only source change.

Both new layouts now complete final routing and verified ingestion. Detailed
routing takes 3.41/1.80 s and peaks at 844/735 MiB. All 44 enabled-net wire lengths
match OpenDB (226779 DBU total, maximum difference 4.55e-13 DBU); DRC reports are
empty and native/Python features agree byte-for-byte. Excluding left-edge I/O
moves eight of 16 pins, reducing the left-edge count from eight to zero; both
feature and label digests differ. `memory_proxy_track_fix/io_pair_audit.json`,
`final_source_replay.json` and the per-case receipts retain the evidence.
The verified pool now contains eight samples across four designs, with two
macro-proxy samples. The full hash/wire-length audit checks 4006 nets and
14919792 DBU. These proxies have estimated zero-internal-delay timing and no
physical power pins; they do not establish characterized or powered SRAM
coverage. All DRC labels remain zero, and original CNN/Eyeriss routes and in-loop
comparisons remain required. The previous five candidates used the six-sample
pool. The separate `pretraining_candidate_eight_mps/` completed 35 epochs in
43.06 s, training on GCD/adder/memory with LFSR used for model selection. Its one
chronologically scoreable LFSR query has hotspot overlap .08605 and SSIM .16964,
versus base RSMT .15067/.30380. NRMS is .23185 versus .23354. Native output is
byte-deterministic and passes mixed tolerance with 1.79e-7 maximum absolute
error; near-zero raw relative error reaches .00336 and exceeds a uniform 1e-4
bound. `eight_sample_training_comparison.json` retains the complete scores,
coverage and lineage. This candidate also remains unbundled; no original-design
unseen or in-loop quality claim is made.

The frozen eight-sample, three-fold unseen study also completed in 186.24 s.
All six queries score under both protocols with verified held-out lineage.
Chronological hotspot overlap is .10276/.20005/.10796 for GCD/adder/memory,
versus RSMT .14812/.37899/.26541. The corresponding SSIM scores are also lower;
no fold model is promoted. `expanded_unseen_folds/summary.json` retains full
metrics and support selections. LFSR lacked an earlier other-design support in
that snapshot, so it was not presented as a fourth completed fold.

A new, actual `lfsr_legacy_io_noleft/` route now preserves the retained legacy
native placement/netlist, releases signal I/O and replaces it on M4/M5 excluding
the left edge. It uses explicitly reconstructed legacy control metadata, not a
new native graph census. All final stages pass; detail routing takes 5.20 s at
773 MiB, DRC is empty, and all 136 enabled-net wire lengths match OpenDB exactly
(210967 DBU). Features and labels differ from both prior LFSR layouts. The
previous eight records and timestamps are unchanged. This later observation
makes all four supplemental folds feasible without retimestamping history.
`nine_sample_unseen_folds/` completed all four from scratch in 274.68 s, with
each test design excluded from gradients, model selection and ancestors.
Chronological scoring covers 8/9 queries (one missing-earlier-support skip);
other-design-only scoring covers 7/9 (two historical LFSR skips). Every scored
query passes unseen-lineage checks. Chronological hotspot overlap is
.10070/.14685/.21999/.11517 for LFSR/GCD/adder/memory, versus RSMT
.12552/.14812/.37899/.26541. Every fold/protocol also has lower SSIM than RSMT;
none is promoted. `nine_sample_unseen_folds/summary.json` retains the full
coverage, support selections and skip reasons. These are supplemental small-design
scores, not original CNN/Eyeriss or in-loop U6. The verified pool now has nine samples (3 LFSR and 2 each of
GCD, adder and memory), 4142 wire-length checks and 15130759 DBU, all clean-DRC.
All earlier model results retain their frozen six/eight-sample inputs.

The active large CNN still uses frozen, pre-fix proxy masters. A read-only
`cnn_frozen_proxy_access_risk.json` audit finds 140 of 283 pins in each of its two
legacy memory proxies without preferred M1 track intersections. This is a
geometric access risk, not an observed full-CNN detailed-route failure. Live
inputs and the guarded driver remain unchanged; any correction requires a
separate checkpoint copy and fresh physical/access validation.

A controlled known-pair diagnostic checks training capacity separately from
cross-design generalization. With one actual GCD query/support pair, no
augmentation, and 500 steps, clamped normalized MSE falls from .07449 to .01583
(the zero predictor is .10633). Its native seen-query hotspot overlap is .52841
versus RSMT .14103; this query was optimized directly, so this is not held-out
quality. `overfit_diagnostic/` weights are diagnostic-only and never published
to a pool. Its original raw native/PyTorch comparison failed at six tiles,
or eight after input normalization precision was aligned. The native arithmetic
fix now passes every normalized and calibrated tile at the unchanged tolerance.
`numerical_trace/production_comparison.json` records a rebuilt production check
against the independently instrumented candidate; their outputs are byte-equal.

The audit independently found a preprocessing discrepancy: native support labels
use double subtraction/division followed by a float32 cast, while training used
float32 arithmetic throughout. `normalized_label_tensor` now mirrors native
precision for query targets and support labels. The offline smoke checks exact
f32 values with a non-binary range and constant-label handling; it passes in
16.71 s, and all 13 offline-tool targets pass. Input tensor layout, inference ABI,
and user-visible defaults are unchanged; the source fingerprint distinguishes
new attempts from the earlier reported cohorts. Normalization alignment does
not by itself resolve the native arithmetic discrepancy. The rejected bias-order
hypothesis made no native-kernel or golden-test change.

The separate arithmetic fix matches CPU bilinear evaluation order for small
outputs (four weighted samples) and larger outputs (separable interpolation),
and sums each channel mean in blocks of 16. These orders are verified against
[the pinned PyTorch CPU interpolation source](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/UpSampleKernel.cpp)
and [summation source](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/SumKernel.cpp).
A new synthetic odd-grid cancellation case fails one tile under the old native
kernel and passes all tiles after the fix. Both prior golden cases are retained
unchanged. Fresh 256x256 checks pass with one and five supports; reused-query
times are 1.30 s and 2.78 s on this host. This closes the observed diagnostic
numerical failure, without establishing cross-platform bitwise identity or
improving the reported held-out quality. All weights remain private.

## Evaluation and checkpoint lineage

`evaluate.py` scores the native calibrated utilization and DRC predictions, with
optional bbox/RSMT baselines on the same routed geometry. It needs no Torch:

```
python -m tools.uroute.evaluate --pool /project/uroute --output evaluation \
  --query-top held_out_top --require-unseen-design --shots 1 --fallback skip \
  --rudy-metrics bazel-bin/tools/rudy_metrics
python -m tools.uroute.evaluate --pool /project/uroute --output zero_shot \
  --query-top held_out_top --require-unseen-design --mode zero-shot --shots 5
# A specific ordered support set, with no query self-support:
python -m tools.uroute.evaluate --pool /project/uroute --output chosen \
  --query QUERY_ID --mode chosen --support SUPPORT_ID --shots 1
```

Default `--mode earlier` considers only samples whose `created_at` is strictly
less than the query's publication time. Tied timestamps are not earlier. Among
those candidates it applies the placer's newest-first same-top/technology
selection and nearest-feature fallback. `zero-shot` forces nearest support from
other declared tops, still using earlier publications. `chosen` records a
manual support set without claiming chronological replay. All modes prohibit
query self-support, duplicate support IDs and incompatible/failed supports.
`--fallback skip` also refuses an eligible pool smaller than the requested shots.
Publication time is what the pool knows; it is not proof of original design-run
chronology when historical results were imported later.

The output retains a canonical manifest snapshot, checkpoint hash and complete
recorded provenance, per-query predictions/digests, selected IDs, timings and
all U0 metrics. Undefined metrics retain null values and explicit denominators
in macro averages. Failed/incompatible queries and insufficient support remain
in coverage as skipped queries with reasons. Query-weighted and per-design
averages are separate; optional baseline averages use the same scored queries.
Scores and coverage are also grouped by actual support selection, so nearest
fallback queries cannot be mistaken for same-design few-shot measurements. Use
`--fallback skip --shots K --min-shots K` for fixed-K earlier same-design
evaluation; the separate zero-shot run forces other-design support.
Each invocation keeps its manifest, predictions and report under a separate
`runs/run_*/` directory. The top-level `evaluation.json` is replaced atomically
only after a complete run; earlier reports continue to reference their own
artifacts. An absent baseline has zero defined contributions. No failed route quietly
becomes a zero-error result.

For a real leave-one-design-out fold, exclude the test design from both gradients
and model selection. Early stopping can use one task of the other two designs,
while the remaining tasks still supply two gradient-training designs:

```
python -m tools.uroute.train --pool /project/fold-c --from-scratch \
  --exclude-design C --validation-task '["A","lef:TECHNOLOGY_HASH",0]'
python -m tools.uroute.evaluate --pool /project/fold-c --query-top C \
  --output /project/eval-c --require-unseen-design --shots 1 --fallback skip
```

`cross_validate` runs an explicitly configured set of these folds serially. It
uses the existing trainer/evaluator, with test-design exclusion and unseen-design
lineage checks mandatory. A recipe names each test top and an early-stopping
task belonging to another design, for example:

```json
{"folds":[
  {"test_top":"A","validation_task":["B","lef:TECH_HASH",0]},
  {"test_top":"B","validation_task":["C","lef:TECH_HASH",0]},
  {"test_top":"C","validation_task":["A","lef:TECH_HASH",0]}
]}
```

Use the actual technology hash for each validation task. The metadata-only
readiness check needs no Torch and creates no weights:

```
python -m tools.uroute.cross_validate --pool /project/uroute --config folds.json \
  --output /project/leave_one_out --from-scratch --check-only
# Run in the isolated Torch environment after coverage is ready:
python -m tools.uroute.cross_validate --pool /project/uroute --config folds.json \
  --output /project/leave_one_out --from-scratch \
  --rudy-metrics bazel-bin/tools/rudy_metrics
```

Output must be outside the source pool. Each invocation retains a separate
`run_*/` manifest snapshot and independent fold models/manifests. Immutable
sample objects are hard-linked (copied across filesystems); never edit these
shared data files in place. Their hashes are verified before fold training.
The readiness report lists placements per `(top, technology)` task and the
additional distinct placements needed to reach two. Two design arms with
different LEF fingerprints are separate tasks; their samples cannot stand in
for a query/support pair. Each fold also lists incomplete training tasks. This
is a metadata check, not evidence that a pending route will complete.

A read-only audit of the four currently pending original-design arms finds
that the two CNN arms have different technology hashes (the LiveHD arm adds
its memory-proxy LEF). Each would still be a singleton training task after
one successful route. Each needs another distinct routed placement before
an original-design fold that trains on it can run. The two Eyeriss arms share
a technology hash. These are conditional counts, not completed samples;
`repros/uroute_real_small_corpus_20261025/task_coverage_audit/pending_task_projection.json`
retains the exact LEF hashes. The current original-fold readiness check remains
`insufficient-coverage`; neither the live jobs nor the canonical pool changed.

Two follow-up CNN I/O variants are now queued under
`repros/uroute_cnn_support_variants_20261025/`. The single worker waits for all
four original routing processes to exit. Each variant additionally requires its
own source's completed final-route receipt and every routing-stage marker.
It reads the retained post-CTS checkpoint into a separate job, replaces signal
pins on M4/M5 with the left edge excluded, and verifies every instance's master,
location, orientation and placement status is unchanged. The source's fixed
macro frame, LEFs, clocks and CTS tree are retained. These are explicit
post-CTS I/O experiments, not fresh final-GP auto-versus-RSMT results or default
flow success. All large stages use the existing global lock and resource guards;
changed inputs or a prior attempt stop the worker rather than triggering a retry.

The preparation Tcl passes bounded checks on an LFSR checkpoint (186 unchanged
instances, 11 moved ports) and a memory-proxy checkpoint (175 unchanged instances,
including one fixed macro, 16 moved ports), each under one second and 140 MiB.
The first failed path-name and tiny-die spacing attempts are retained. Frozen
CNN scripts and LEF identity checks are in `preflight.json`; no large variant
has executed and none is counted as a support sample. A variant enters the pool
only after fresh pin access, complete detailed routing and verified extraction.
It retains the source's physical-only, uncharacterized proxy limitations and
does not correct the frozen legacy CNN proxy geometry.

The input pool is not updated. Fine-tuning without `--from-scratch` requires
complete ancestor lineage excluding each fold's test design. Both earlier and
zero-shot protocols run with `--require-unseen-design`; skips remain in coverage.
The combined report averages design means with explicit defined/undefined
design counts, records whether every fold/protocol has scores, and separately
records whether all requested queries were scored. An evaluated report is not
proof of full coverage or a quality win.

The real route-compare recipe and readiness audit are retained in
`repros/uroute_validation_20261024/route_compare_folds.json` and
`leave_one_out_preflight/`. They currently reject training: CNN/Eyeriss have no
completed routed query/support pairs, and LFSR lacks earlier other-design
zero-shot support. Historical imports must not be retimestamped to fake that
coverage. The three-fold offline structural smoke passes in 16.1 s with an
unchanged source pool; its constant synthetic labels exercise orchestration,
not learned prediction quality. Run it explicitly in the Torch environment:

```
python -m tools.uroute.cross_validate_smoke --native bazel-bin/tools/uroute/native
```

Metric 0 is utilization and metric 1 is DRC. `--validation-task` is mutually
exclusive with `--holdout`. Excluded designs never enter the training dataset
identity, sample set or validation losses. An existing checkpoint cannot
initialize such a fold unless its complete recorded ancestry also excludes
the test design. Deliberate `--from-scratch` creates a new lineage. The manifest
records `seen_designs` (gradients, early stopping and known ancestors),
`lineage_complete`, `excluded_designs` and the validation task. Ordinary
fine-tuning preserves the union of seen designs. Unknown imported ancestry
remains incomplete; it cannot support an unseen-design claim. Design separation
is checked using the pool's declared `top` identities.

The default evaluator permits descriptive in-sample scoring, recording the
unseen-design audit per query. `--require-unseen-design` rejects an incomplete
lineage or a query top already seen by gradients, model selection or ancestors.
Support labels from earlier placements of an otherwise unseen test design are
permitted at inference; they do not update weights. A separate project pool
per training fold keeps its immutable checkpoint and provenance reviewable.

The optional CPU smoke now also checks excluded-design training, rejection of
contaminated initial weights, earlier and zero-shot evaluation, and calibrated
native/PyTorch agreement on a newly trained synthetic checkpoint:

```
python -m tools.uroute.train_smoke --native bazel-bin/tools/uroute/native
```

The 2026-10-24 run completed in 15.13 seconds. Scratch/fine-tune/excluded-fold
training took 1.47/1.53/1.10 seconds respectively; native evaluation took about
2.24 seconds per one-shot query, and maximum calibrated native/PyTorch absolute
error was 1.05e-7. The temporary synthetic checkpoint is deleted by the smoke;
these results do not establish prediction quality or provide pretrained weights.
At that smoke's original checkpoint the real pool contained only LFSR. It now
also has two GCD and two IWLS-adder placements (see the real-corpus notes).
Isolated real-data candidates now exist, but their measured hotspot quality
does not justify bundling them. The original route_compare leave-one-design-out
and CNN/Eyeriss in-loop requirements remain unmet.

## Public source check for U7 (2026-10-24)

The [author's publication entry](https://yibolin.com/publications/) lists URoute
with its DOI; that entry has no code/checkpoint link. The
[author's software page](https://yibolin.com/softwares/) has no URoute entry.
These checks and a title/code search did not identify an author release. This
is not proof that no release exists; architecture v1 remains a documented local
implementation rather than a checkpoint-compatible reproduction.

[CircuitNet's repository license](https://github.com/circuitnet/CircuitNet/blob/main/LICENSE)
is BSD-3-Clause. The separate
[N14 dataset card](https://huggingface.co/datasets/CircuitNet/CircuitNet/blob/main/README.md)
also declares BSD-3-Clause; its current
[raw-data tree](https://huggingface.co/datasets/CircuitNet/CircuitNet/tree/main/CircuitNet-N14/raw_data)
contains N14.lef, netlists, RTL and DEF archives. This current tree is stronger
evidence of raw-data availability than the older website's request-only text.
Its supplied [feature descriptions](https://github.com/circuitnet/circuitnet.github.io/blob/vue/docs/intro/overview.md)
use cell counts and global/early-global-route congestion, which do not directly
satisfy our physical cell-area and detailed-wire label contract. Raw geometry,
wire/DRC availability, control roots and technology collateral must be checked
on a bounded subset before admitting data as feature/label version 1.

The linked [CircuitNet 3.0 dataset card](https://huggingface.co/datasets/SKLP-EDA-LAB/CircuitNet3.0)
declares Apache-2.0 and describes an RTL/netlist/timing/power release with layout
uploads in progress. It is not evidence that all routed labels needed here are
available. No public dataset or checkpoint was imported, relabeled or bundled
by these checks. A future bundled checkpoint must retain the actual source
revision, sample IDs, preprocessing/label conventions, licenses/notices and
training lineage in its provenance.

A bounded raw-data audit on 2026-10-25 pins N14 revision
`df74f33e302f927bf0548dc0d7936ca43b634f2a`. Reading only the first 24 MiB of
the Vortex-small DEF archive recovered one complete 205604891-byte detailed-route
DEF. It contains 336666 components using 192 masters, all present in the
downloaded LEF, and 119951 regular nets with 119708 routed clauses. This proves
wire geometry is available; it does not prove a clean, fully connected route.
The LEF has 770 macro definitions but no top-level routing-layer definitions.
The subset also lacks the Liberty/control-role metadata required by our native
geometry path and a raw DRC marker report. Upstream extractor blob
`e7c2aea43eef8c0b8c4533e49ca51c436c2d9f91` increments every covered GCell for a
DRC bounding rectangle, whereas label v1 counts each marker once at its center.
Precomputed DRC grids therefore cannot be relabeled as v1 without a verified
conversion. No downloaded public sample entered a training or support pool.

CircuitNet 3.0 revision `5683d95097bb27bb380f135101d7da6991025557` was also
checked by reading its ZIP central directory, rather than downloading the
1.03 GB archive. All 57975 directory entries were inspected: none has a DEF,
ODB, LEF or Liberty filename extension. Its dataset card explicitly excludes
commercial PDK/standard-cell collateral and says layout uploads are incremental.
It does not currently supply the required routed inputs through those entries.
These are bounded-source findings, not a claim that missing N14 collateral is
absent everywhere in the release. Source revisions, archive directory/member
hashes, license notices and admission decisions are retained under
`repros/uroute_validation_20261024/public_source_audit/`; the authoritative
records are `admission_audit.json` and `circuitnet3_archive_audit.json`.


## ORFS post-route hook

The retained `repros/route_compare_20261019/run.py` now invokes the explicit
post-route adapter only when `LHD_UROUTE_POOL` is supplied. This is an offline
benchmark-harness setting, not an inference control in LiveHD:

```
LHD_UROUTE_POOL=/project/uroute python repros/route_compare_20261019/run.py lfsr_livehd lfsr_openroad
# The same adapter is usable independently of that retained driver:
python -m tools.uroute.postroute --job /path/to/orfs-job --pool /project/uroute \
  --source livehd --controls /path/to/native_qor.json
```

Completed jobs can be ingested without repeating synthesis or routing. The
adapter reads `environment.json`, final DEF (plain or gzip), the DRC report and
explicit clock/reset metadata. It combines compressed Liberty files into an
immutable content-named input, uses the job's LEFs and target utilization, and
reuses an available LiveHD feature export. Input overrides are explicit CLI
arguments. Environment file lists accept JSON arrays or shell-style quoting;
relative environment paths resolve from the job directory. A missing retained
technology file produces an error instead of a filesystem search or guessed
replacement.

Controls come from `--controls`, `job/uroute_controls.json` or a native QoR file
containing `prects_controls`. Roots are never guessed from signal names.
`--status auto` requires terminal `status: completed`; a stale completion marker
cannot override failure. `--status routed` is an explicit caller assertion used
by the driver after its successful route stages. Failed-route ingestion uses
`--status unrouted --def PLACEMENT_DEF --last-global-overflow MAP`; a missing
overflow map is not fabricated. For a failure before any global-route resource
map exists, provide an explicit `--overflow-unavailable REASON`; the record then
contains its features and reason, with neither labels nor a fabricated map.

`job/uroute_ingest.json` records the sample ID, input/control identity and target
utilization source. Requested ingestion failures are recorded separately from
successful routing, and make the harness invocation fail. With the environment
setting absent, the harness keeps its existing routing behavior. No training
is started by the hook.

The hermetic ingestion smoke covers compressed libraries, automatic completed
job ingestion, replay without duplicate support shots, explicit control
requirements and rejection of a failed job with a stale completion marker.
The actual hook was also run successfully on both frozen LFSR arms, producing
two samples in `repros/uroute_validation_20261024/hook_pool` in about 1.4 seconds
per job with 316 MiB or less process-tree peak memory. The legacy controls are
recorded reconstructions, not contemporaneous native exports. No router or
synthesizer was launched by that hook validation.


## ODB input and failed-route resources

```
python -m tools.uroute.ingest --odb 6_final.odb.gz --openroad /path/to/openroad \
  --lef tech.lef --lef cells.lef --liberty cells.lib --controls native_qor.json \
  --drc 5_route_drc.rpt --pool /project/uroute --source livehd --utilization .55
python -m tools.uroute.postroute --job /path/to/job --pool /project/uroute \
  --source livehd --status unrouted --odb 5_1_grt.odb --openroad /path/to/openroad
# Explicitly retain an earlier failure when there was no global-route map:
python -m tools.uroute.postroute --job /path/to/job --pool /project/uroute \
  --source livehd --status unrouted --odb 4_1_error.odb --openroad /path/to/openroad \
  --overflow-unavailable 'pin access failed before global-route resource data existed'
```

The external Tcl adapter exports the physical DEF from the selected ODB.
For a routed sample it also runs OpenROAD's detailed wire-length report, which
is checked automatically against the DEF parser before publication. The sample
retains `source_odb_sha256` and an `odb_export.json` sidecar containing source,
OpenROAD binary and adapter identities. Plain and gzip ODB files are supported;
`--odb-timeout` defaults to 300 seconds for this external export. This conversion
never starts routing or training and does not require OpenROAD's Python binding.

For an unrouted ODB with no explicitly supplied overflow map, the adapter reads
`dbGCellGrid` resource data. It sums `max(usage-capacity,0)` over routing layers
and area-averages that piecewise GCell field onto the core's 256x256 grid.
The final GCell interval extends to the die edge, matching OpenROAD's partial
last-cell convention. Core/DBU agreement with the physical feature reader is
required. Capacity, usage, overflow totals, source-grid dimensions and units
are retained. These are last-global-route diagnostics, not detailed-wire
training labels. Zero-capacity bins with positive usage retain their excess;
there is no division by zero or substitution of a clean result.

OpenDB can lazily create an empty, zero-filled resource map. The adapter rejects
an absent grid or an all-zero capacity census. A clean map is accepted only
when actual positive-capacity resources exist. By default missing resource
data is an error. `--overflow-unavailable` explicitly records a nonempty reason
for the absence, and is valid only on an unrouted sample without a supplied map.
Such samples have `last_global_overflow_status: unavailable`, no overflow file,
and no labels. They are excluded from both native support and training just as
other unrouted records are. No inferred zero map is written.

Validation on the frozen LFSR final ODB reproduced the DEF ingestion's feature
and label bytes exactly; all 136 enabled-net lengths also matched the independent
ODB report. Exporting the stored global-route resources yielded a measured clean
map with capacity sum 13091 and usage sum 4455. A pre-route ODB with no resource
map was rejected. A separate synthetic injection of three overflowing tracks
into one GCell preserved the expected physical integral to within 0.001 DBU^2
tracks; that injection was a decoder test and was not admitted as routed data.
Artifacts are under `repros/uroute_validation_20261024/odb_*`.

FastRoute can write intermediate aggregate 2D congestion-marker reports before
there is a final ODB. Retain their emitted evidence separately:

```
python -m tools.uroute.overflow --report congestion-5.rpt \
  --frame /project/uroute/pool/SAMPLE_ID/sample.json --output new_checkpoint_dir
```

This strict parser preserves the report, physical frame and checksums, and
area-averages the emitted usage-minus-capacity marker rectangles onto 256x256.
It rejects incomplete/duplicate markers, mismatched counters, other report types
and empty reports. The output is `emitted_overflow_lower_bound.bin`, never a
training label or a complete overflow/resource map. Marker categories can be
capped and shuffled: unlisted cells are unknown, not measured clean. The JSON
records scope, units, marker counts and the lower-bound limitation. Iteration
filename tags may restart inside FastRoute and do not establish chronology.
The helper does not ingest or select these maps as supports.

The live CNN report tagged 5 reached the producer's default 10000-marker cap in
each direction. Its 20000 emitted markers sum to 17880450 excess-demand counter
units; this is a subset sum, not total routing overflow or physical wire demand.
The retained snapshot is under
`cnn_livehd_aligned_allow/intermediate_overflow/checkpoint_5_snapshot/` in the
recovery directory. The raw report and the prior metadata are retained alongside
the explicit cap audit. Fully routed wires/DRC remain the only training labels.

## Supplemental observations without duplicate support shots

Re-importing a matching DEF identity, feature/label digests and physical/config
identity keeps the first immutable sample ID and timestamp. Importing its ODB
container or adding a later feature-gap/verification report does not create a
second support shot. The original sample and primary maps stay unchanged.
Additional metadata and changed sidecars are retained as content-named entries
under `observations/`, committed before the manifest's `observations` list is
updated. That list is outside `samples`, so it does not enter support or training.

Each `observation.json` identifies its `sample_id`, `sample_directory`, complete
observed metadata and hashes of changed sidecars stored in that observation's
directory. Unchanged files, including the primary maps, resolve from the original
sample directory. The publisher returns the original sample fields plus an
`observation` descriptor when it records supplemental evidence. For example, a
feature gap added after initial ingestion is in the observation's metadata.
Replaying the same supplemental evidence preserves its observation ID and does
not create extra entries. Writers verify the original sample before reusing it;
readers still receive the original immutable feature/label pair.

The ingestion smoke covers this enrichment and replay. The real C++ pool-reader
test also checks that supplemental observations and explicitly unavailable-map
failures do not become new support samples.

## CNN reference recovery in progress

The original `route_compare_20261019` physical checkpoints, technology collateral
and routing executable remain unchanged. The optional post-route driver hook
and ingestion records are separate from those physical inputs. Recovery work
is retained separately in `repros/uroute_route_recovery_20261024/`:

- The original CNN CTS failed placement with 11547 cells and a default window of
  500 sites by 100 rows. An explicit full-core search legalized all 455236
  movable cells, kept all 67 fixed macros unchanged, and passed whole-design
  legality. Runtime was 329.26 seconds; peak process-tree RSS was 14301 MiB.
- Resuming the remaining post-CTS repair completed in 1562.21 seconds with
  17224 MiB peak RSS. It left setup/hold violations reported and wrote a legal
  CTS checkpoint. This is not timing closure.
- Pin access then failed on a retained RAM pin at local x=216 nm against the
  router's M3 tracks at phase 9 nm. The existing LiveHD track-alignment code has
  a regression for precisely that abstract. An offline bounded proposal moved
  67 macro origins by at most 33 nm Manhattan distance. Applying it in a new
  copy passed whole-design legality, restored all fixed statuses and left
  standard-cell coordinates unchanged. No LEF pin geometry was modified.
- The phase-aligned copy completed pin access: zero standard-cell pins without
  access and zero macros without access. Global routing then failed with
  `GRT-0228`: an initial horizontal edge had usage 3326 against the congestion
  guard threshold 3300. This is a separate failure from the repaired pin access.
  The attempt took 1361.16 seconds and peaked at 35945 MiB process-tree RSS;
  minimum system reserve was 25.75 GiB. Its failed global-route ODB is retained.
  The frozen router predates the upstream fix that makes this guard honor
  `-allow_congestion`; no router default or frozen executable was changed.
- The earlier pin-access failure was ingested as an unrouted diagnostic with an
  explicit unavailable-resource-map reason. It supplies no training label and
  is excluded from native supports. The later failed global-route ODB also
  ingests: its stored 3418x3418 per-layer map has capacity 841354372 and usage
  449879286, with zero stored overflow. That snapshot does not represent the
  transient 2D demand that tripped the guard and does not establish routing
  success. Both observations remain explicitly unrouted and excluded from
  supports/training. Extraction took 144.26 seconds with 4021 MiB peak RSS;
  checkpoint provenance hashes stream the input rather than loading it whole.
  Eyeriss full-core legalization also passed: 512951 movable cells, zero
  failures, all 1040 fixed macros unchanged, max displacement 74.1 um and HPWL
  +4%. It took 306.45 seconds with 9788 MiB peak RSS. Its CTS timing repair then
  hit the explicit 1800-second wall-time guard, with 4454 MiB peak RSS and
  37.82 GiB minimum system reserve. The last saved legal pre-repair checkpoint
  was ingested as unrouted, explicitly distinguished from the in-memory state
  at timeout. It has no global-route map or labels and is excluded from
  supports/training. A verified-LEF track-phase proposal for all 1040 Eyeriss
  macros also needs at most 33 nm movement; it is a proposal only, not an
  applied or legality-verified frame. `active.json`,
  stage logs and guard result JSON files are authoritative. No final routed
  CNN/Eyeriss result or estimator-quality conclusion is claimed yet.
- A separate newer headless OpenROAD was built and frozen for a labeled
  `-allow_congestion` recovery experiment. It reproduces byte-equal LFSR feature
  and label maps from the retained final ODB, including all 136 independent
  wire-length checks. Its first zero-capacity smoke exposed an empty monotonic
  cost-table crash. The owning OpenROAD source now retains an index-zero entry
  when aggregate capacity is zero; positive-capacity entries are unchanged.
  The new integration regression, strict horizontal/vertical guard regressions,
  two existing routing-guide comparisons and overflow unit tests pass (six
  targets, each under 2 seconds). The corrected frozen v2 executable passes the
  exact earlier crash command. The original frozen executable is unchanged.
- `cnn_livehd_aligned_allow/` started after the Eyeriss job stopped. It retained
  the same phase-aligned CTS input, passed pin access again, and reached
  congestion-removal iteration 8/30 before its 7200.58-second wall-time guard.
  Peak process-tree RSS was 35404 MiB; minimum system reserve was 23.94 GiB.
  No final global-route ODB or routed sample was produced. It explicitly
  permits congestion in global routing and antenna
  repair. It still requires completed detailed routing, actual DRC output and
  verified ingestion before producing a routed sample. `queue.json` records
  whether it is waiting or running. This policy differs from the original
  strict attempt and is retained in `experiment.json`; its timeout is a
  separate terminal result, not a router assertion failure.
- A separate checkpointed recovery writes pin access to an ODB/SDC, closes that
  process, and starts global routing in a fresh process. On the retained small
  LFSR, all 448 connected standard-cell preferred accesses survived the round
  trip; pin access and fresh-process global routing passed in 2.08 and 0.89
  seconds (164 and 304 MiB peak process-tree RSS). Resume created no new
  attempts, and a changed checkpoint SDC was rejected. The remaining detailed
  route, fill and final-report stages also passed, each in under 6 seconds,
  with an empty DRC report and no stage reruns on full resume. This small
  validation was not added to the training pool. The validation is in
  `checkpoint_roundtrip_lfsr_v2/validation.json`; it does not prove the CNN
  macro-access round trip. Tcl does not expose the nested macro access-index
  container, so the large design still needs an actual routing run.
  `queue_checkpointed_cnn.py` watches the existing coupled attempt without
  interrupting it. Only a recorded global-route wall-time guard failure permits
  one fresh `cnn_livehd_checkpointed/` attempt; success needs no recovery, and
  other failures require diagnosis. Pin access has a one-hour budget and global
  routing a separate six-hour budget, with the same 40 GiB process-tree cap,
  4 GiB host reserve and disk reserve. The same physical CTS frame, 30 congestion
  iterations and explicit congestion policy apply. Each stage has a separate
  immutable attempt directory and completion receipt hashing inputs, executable,
  driver, script and output companions. A failed attempt is preserved; neither
  the watcher nor driver repeatedly retries it. Final detailed-route completion
  and verified ingestion remain necessary before admitting a routed sample.
- `eyeriss_livehd_cts_extended/` is a separate CTS repair attempt from the
  hash-verified legal pre-repair ODB/SDC, using the original frozen router with
  a 7200-second wall limit and 24 GiB process-tree cap. It preserves the earlier
  1800-second timeout. This extended attempt also stopped at its wall guard
  (7200.16 s, peak 4704 MiB), with no final repaired CTS checkpoint.
  `eyeriss_livehd_unrepaired/` is a distinct queued, timing-unclosed experiment
  from the last saved legal post-CTS pre-repair ODB/SDC. After the CNN releases
  the global lock, it applies the retained phase proposal, requires all 1040
  macros to retain fixed states and whole-design legality, then uses separated
  pin-access/global/detail stages while skipping optional timing repair.
  Neither experiment supplies routed labels yet. A replaced temporary lock
  inode briefly allowed the extended CTS to overlap the older CNN attempt;
  Eyeriss's owned router was suspended for 72.44 seconds until the original
  CNN queue was terminal. Both lock inode numbers and suspension times are
  recorded in `serialization_pause.json`; CTS wall time includes that pause.
  The checkpointed CNN now owns the routing lock; the unrepaired Eyeriss
  experiment waits on the same current lock inode. CNN pin access has now
  completed in 1155.65 s with a 35541 MiB process-tree peak and zero macro pins
  lacking access. Its fresh global-route process confirms all 1443250 standard
  preferred access entries survived the checkpoint reload; global routing is
  running and final routing remains unverified. Running drivers are not
  edited or automatically retried.

Historical feature-version-1 exports were reconstructed from the retained,
hash-verified LiveHD pre-CTS DEFs. Both reconstructions match native/Python
features byte-for-byte. The comparisons below use unrouted recovery frames,
not final routed labels; they do not establish learned prediction quality.

| Recovery frame | RUDY RMSE | PinRUDY RMSE | MacroRegion RMSE | CellDensity RMSE |
| --- | ---: | ---: | ---: | ---: |
| CNN post-CTS | .015843 | .028159 | 0 | .051261 |
| CNN phase-aligned post-CTS | .015843 | .028177 | 0 | .051261 |
| Eyeriss last legalized checkpoint | .075598 | .049197 | 0 | .076731 |

These are normalized channel RMSEs against the reconstructed historical export.
The isolated CNN phase correction changes RUDY by 3.07e-6 RMSE and PinRUDY by
.000985; density and the coarse macro map are unchanged. Source identities and
full statistics live under `cnn_feature_gap/` and `eyeriss_feature_gap/` in the
recovery directory. Supplemental pool observations retain the reconstructed
feature files and gap metadata without adding samples or altering the original
sample IDs. Large immutable sidecars are verified with streaming hashes.

`//tools:macro_track_align` writes a bounded proposal TSV using the existing
macro-track legalizer and actual DEF tracks. It never modifies a design;
`--move-fixed --max-shift-dbu N` is required to propose fixed-macro movements.
Without that flag it audits macro phases. It rejects an existing output file
and any movement beyond the explicit bound. The separate recovery Tcl applies
an explicit proposal to a copy and verifies legality. Existing macro-track and
routing-grid tests pass. Fresh in-loop experiments must provide the actual
router track grid through `synth.place.tracks`; substituting LEF offset defaults
would reproduce the legacy handoff mismatch.

The fifteen-sample, seven-design held-out study completed and was rescored after
correcting native support accumulation to preserve Torch's separate f32
multiply/add boundary. All seven maximal-support numerical spot checks pass at
the unchanged tolerance; the hermetic golden includes a four-shot cancellation
regression that fails with the former fused accumulation. Quality remains below
RSMT on mean hotspot overlap and SSIM, so no weights are promoted. Earlier
support scores 14/15 queries and zero-shot 13/15, all with verified unseen lineage.
See `repros/uroute_fifteen_sample_unseen_20261025/corrected_study_summary.json`.
Original-design coverage and CNN/Eyeriss in-loop routed comparisons are still
required; the small corpus has no positive-DRC labels.

Whole-flow physical replay fingerprints requested URoute pool contents before
its cache lookup, including bundled-directory resolution. An unchanged pool
reuses final state; an incompatible manifest or published weight update forces
a fresh placement. This outer replay gate and the placer's own cache both cover
learned inputs. The CLI smoke exercises each path with repository-owned data.

Expanded checks also exposed an attention sigmoid difference between system
`expf` and the CPU Torch reference's SLEEF exponential. Native inference now
uses a scalar f32 version of the pinned polynomial and explicit FMA order;
the traced zero-shot counterexample matches all 228 intermediate tensors byte
for byte. The implementation retains SLEEF's copyright and Boost license,
without adding a runtime library dependency. The fifth hermetic golden case
catches this difference independently of the support-accumulation correction.
Source provenance: [SLEEF's pinned exponential](https://github.com/shibatch/sleef/blob/5a1d179df9cf652951b59010a2d2075372d67f68/src/libm/sleefsimdsp.c#L2029)
and [Torch's sigmoid kernel](https://github.com/pytorch/pytorch/blob/5c4886908584029761b579af026dcfb627c84070/aten/src/ATen/native/cpu/UnaryOpsKernel.cpp#L32).

With both corrections, all **27 scored held-out queries** (earlier and zero-shot
protocols, seven excluded designs) match frozen CPU Torch exactly across
3,538,944 calibrated output values. Constant DRC outputs exercise the calibration
bypass; they do not demonstrate learned nonconstant DRC quality. Fresh predictions
were also rescored with the unchanged baseline, and the earlier quality conclusion
is unchanged: no weights are promoted. Final evidence is in
`repros/uroute_fifteen_sample_unseen_20261025/attention_corrected_study_summary.json`.

The fixed-grid scale audit is retained under
`repros/uroute_grid_resolution_20261025/`. Across the 15 small routed samples,
256×256 tiles span 0.023–0.094 µm and can be finer than one nominal routing track.
Retained historical CNN/Eyeriss native frames instead span about 7.60/5.53 µm
per tile. These pitch-based measurements are linked to each routed sample as
immutable provenance observations, preserving the original sample records,
weights and independent support count. Historical original-design frames are
geometry evidence only; their final routed labels are still required.

The post-fix full optimized repository regression completed with the local
DREAMPlace override: **3766/3766 tests passed, all executed fresh**, with a
30-second per-test cap. The slowest test took **17.9 seconds**. Source fingerprints
remained unchanged; the full command took 3591.5 seconds, peaked at 5558 MiB and
retained at least 21.46 GiB OS reserve. Evidence is retained in
`repros/uroute_postfix_full_regression_20261025/summary.json`. This is local
validation, not published-dependency or remote-CI evidence.
