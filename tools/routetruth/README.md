# routetruth: score a congestion estimate against real routing

`routetruth` measures how well a congestion map predicts what a detailed router
actually did. It reads a routed DEF and the router's DRC report, builds label
maps from the routed wires, and scores an estimate against them. It is the only
check in this repository that compares an estimator with real OpenROAD routing.

It contains no model and trains nothing. The decision to keep it is R2 in
[2r-route](../../todo/livehd/2r-route.html).

## What it measures (label definition v1)

Labels are raw maps on a grid over the core (256 x 256 by default), written to
`labels.bin` with three channels:

| Channel | Label | Definition |
|---|---|---|
| 0 | utilization | Maximum over six maps: horizontal and vertical wires of the layer groups M2-M3, M4-M5 and M6 upward (positions in LEF routing order). Each map is the routed centerline length in the tile divided by the nominal track length the group offers there (tile area times the sum of the inverse pitches of the group's layers that prefer that direction). |
| 1 | DRC marker density | Number of detailed-route DRC markers whose bounding-box center falls in the tile. |
| 2 | detour ratio | Routed length of each net divided by its FLUTE tree length, averaged over the tile with the routed centerline length as weight. |

Details that matter when reading a score:

- Only enabled data nets count. Clock and reset nets are classified from the
  Liberty file and the `prects_controls` roots and are left out.
- M1 is local pin access: it is left out of utilization and included in the
  detour length.
- Capacity is the nominal LEF track density. It is not reduced for power,
  blockages, macros or vias, so a utilization of 1 does not mean "full".
- An empty DRC report means no markers. Markers outside the core are counted in
  `extraction.json` and left out of the map.
- The six group maps are also written, to `layer_groups.bin`.
- `extraction.json` lists every net with its routed length, centerline length,
  FLUTE length and detour, the group capacities, and the via and patch counts.

## Scores

`score.py` compares a reference map with a prediction on the reference grid (a
prediction on another grid is area-resampled first):

| Score | Meaning |
|---|---|
| `pearson`, `spearman` | Linear and rank correlation over tiles. `null` when a map is constant. |
| `ssim` | Structural similarity (11 x 11 Gaussian window, sigma 1.5). |
| `nrms`, `rmse` | Root-mean-square error, relative to the reference range and absolute. |
| `hotspot_top5_overlap` | Share of the reference's most utilized 5% of tiles that are also in the prediction's most utilized 5%. Tiles tied at the 5% cutoff count fractionally, so a map with many ties there scores below 1 even against itself. |

Correlation does not show whether the scale is right. In the routed corpus RSMT
correlated with routed utilization but reported about one third of its value,
so compare `rmse` and the map ranges as well.

## Run it on an OpenROAD result

Inputs: the routed DEF (`6_final.def`), the detailed-route DRC report
(`5_route_drc.rpt`), the LEF files, one combined uncompressed Liberty file, and
a JSON file with the `prects_controls` clock and reset roots (the `qor.json` of
the LiveHD placement has them).

```
bazel build -c opt //tools:placement_metrics //tools:rudy_metrics //tools/routetruth:flute_lengths

# 1. Physical geometry of the routed DEF.
bazel-bin/tools/placement_metrics --def 6_final.def --lef tech.lef --lef cells.lef \
  --liberty cells.lib --controls qor.json --output physical.json --route-geometry geometry.json

# 2. FLUTE length of each net (the detour denominator).
bazel-bin/tools/routetruth/flute_lengths geometry.json flute.json

# 3. Labels from the routed wires and the DRC report.
python -m tools.routetruth.extract --geometry geometry.json --def 6_final.def \
  --drc 5_route_drc.rpt --flute-lengths flute.json --output labels/

# 4a. Score native RUDY and RSMT against the routed utilization.
python -m tools.routetruth.baseline --geometry geometry.json --labels labels/labels.bin \
  --output scores/ --bins 64

# 4b. Or score any other map.
python -m tools.routetruth.score --reference labels/labels.bin --prediction estimate.bin
python -m tools.routetruth.score --reference labels/labels.bin --prediction-tsv rsmt_base.tsv
```

Step 3 also reads a gzip-compressed DEF or DRC report (`.gz`). Step 4a runs
`tools/rudy_metrics rudy|rsmt` on a `--bins` square grid, with and without macro
blockages and pin access ("enhanced" and "base"), and writes `baseline.json`.
The estimators use the capacity of all routing layers, including M1; the labels
do not. The report states this.

To check the parsed wire lengths against OpenROAD, write
`report_wire_length -net {*} -detailed_route -file wirelength.txt` from the same
final database and add `--wirelength-report wirelength.txt` in step 3. A missing
net, or a length that differs by more than the report's printed precision and
one DBU, is an error.

## Map files

`labels.bin`, `layer_groups.bin` and `--prediction` files are little-endian:
an 8-byte magic, five `u32` fields (format version 1, channels, bins x, bins y,
normalized flag), one `f64` minimum and maximum per channel, then `f32` values
in channel, x, y order. The magic is `UROUTEM1`, the one the first label corpus
was written with; it is unchanged so those files stay readable. `maps.py` reads
and writes the format and rejects truncated, oversized and nonfinite files.

## Known limitations

1. **Routing off the preferred direction on sky130's top layer.** met5 is the
   only layer of its group, so the group has capacity in one direction only. A
   routed wire that runs the other way has no capacity to be divided by, and
   the extractor stops with "wire demand without preferred-direction group
   capacity" instead of writing a label.
2. **Non-default routing rules.** The DEF reader rejects the non-default
   routing rules that OpenROAD CTS writes, so a routed DEF that carries them
   cannot be scored. The extractor itself also rejects a non-default rule, a
   taper rule or a shield on an enabled net; on a disabled clock or reset net
   it skips them.
3. **Geometry of four nangate45 liteeth layouts is not validated.** The removed
   ingestion step computed placement feature maps from `geometry.json` twice,
   in Python and in C++, and required equal bytes. Four of the seven nangate45
   liteeth layouts failed that check and the cause was not found. The check
   went away with the feature code, so nothing here reproduces it; treat scores
   on those layouts with care.

The extractor also does not prove that a design is routed: it rejects an
enabled net with no wires, but it does not check connectivity.

## Tests

`bazel test --config=backend //tools/routetruth/...` runs the unit tests of the
extractor, the scores and the map files, and `routetruth_smoke`, which runs all
four steps on a small routed DEF.
