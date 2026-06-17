#!/bin/bash
# translate.sh — per-module Verilog->Pyrope translation + fine-grained classification.
#
#   translate.sh <TOP>
#
# Uses the WHOLE soomrv file list with `--top <TOP>` (real submodules, no stubs).
# Runs the full pipeline and records every signal, then classifies the module by
# its FIRST failing stage (the 10 categories the project tracks).  One TSV line
# is appended to $RESULTS; PASS .prp -> soomrv/pass/, fails -> soomrv/fail/ (+.md).
#
# yosys+slang gate: the raw `yosys2 -m slang.so -p read_slang …` parse, run on a
# `!&`/`!|`/`!^`-normalized copy of the tree ($NORM) — yosys-slang's bundled slang
# lacks the chained-unary-reduction parse LiveHD's patched slang has, so `!&x`
# (3 soomrv files) would otherwise fail the whole single-unit read; `!&`≡`~&`
# (reduction NAND) is semantics-preserving.  If the gate STILL fails, yosys+slang
# genuinely cannot read the module -> not worth fixing LiveHD for it.
set -u
LHD=/mada/users/renau/projs/livehd/bazel-bin/lhd/lhd
YOSYS=/mada/users/renau/projs/livehd/bazel-bin/inou/yosys/yosys2
SLANGSO=/mada/users/renau/projs/livehd/bazel-bin/external/+http_archive+yosys_slang/slang.so
ABCLIB=/mada/users/renau/projs/livehd/inou/prp/tests/abc/test.lib
ORIG=/mada/users/renau/projs/soomrv/repo
NORM=/tmp/snorm
HERE=/mada/users/renau/projs/livehd/soomrv
RESULTS=${RESULTS:-/tmp/soomrv_results.tsv}
FILES=$(cat /tmp/soomrv_rel.txt)
SLANG="--single-unit --std latest --allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules --allow-toplevel-iface-ports -Wno-explicit-static -Wno-missing-top"
# read_slang flags.  GATE = raw yosys2 (we add --no-proc).  LHD = via lhd
# --reader yosys-slang, which injects its OWN --no-proc, so DON'T repeat it here.
YSREAD_GATE="--no-proc --single-unit --std latest --allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules"
YSREAD_LHD="--single-unit --std latest --allow-use-before-declare --relax-enum-conversions --ignore-unknown-modules"
LECT=150  # per-LEC timeout (s)

TOP="$1"
W=/tmp/sv2_$TOP; rm -rf "$W"; mkdir -p "$W"
ngraphs() { ls "$1" 2>/dev/null | grep -vcE 'library|srcmap|json'; }
emsg() { grep '"severity":"error"' "$1" 2>/dev/null | grep -oE '"message":"[^"]*"' | head -1 | sed 's/^"message":"//;s/"$//'; }

GATE=NA; RSLANG=NA; LGSLANG=NA; LGYS=NA; PRPGEN=NA; LECSL=NA; LECYS=NA; ABC=NA
MSG=""; kind="-"

# 1. yosys+slang gate (normalized tree)
(cd "$NORM" && timeout 200 $YOSYS -m "$SLANGSO" -p "read_slang --top $TOP $YSREAD_GATE $FILES") > "$W/gate.log" 2>&1
[ $? -eq 0 ] && GATE=PASS || GATE=FAIL

# 2. --reader slang -> prp
(cd "$ORIG" && $LHD compile --reader slang --top "$TOP" $FILES --emit-dir pyrope:"$W/prp" \
   --workdir "$W/c_prp" --emit diagnostics:"$W/d_prp.jsonl" -- $SLANG) > "$W/prp.log" 2>&1
PRP="$W/prp/$TOP.prp"
if [ -f "$PRP" ]; then RSLANG=PASS; kind=$(grep -oE "pub (comb|pipe|mod)" "$PRP" | head -1 | awk '{print $2}'); else RSLANG=FAIL; MSG=$(emsg "$W/d_prp.jsonl"); fi

# 3. --reader slang -> lg (ref)
if [ "$RSLANG" = PASS ]; then
  (cd "$ORIG" && $LHD compile --reader slang --top "$TOP" $FILES --emit-dir lg:"$W/sllg" \
     --workdir "$W/c_sl" --emit diagnostics:"$W/d_sl.jsonl" -- $SLANG) > "$W/sl.log" 2>&1
  [ "$(ngraphs $W/sllg)" -ge 1 ] && LGSLANG=PASS || { LGSLANG=FAIL; [ -z "$MSG" ] && MSG=$(emsg "$W/d_sl.jsonl"); }
fi

# 4. prp -> lg (impl)
if [ "$RSLANG" = PASS ]; then
  $LHD compile "$PRP" --top "$TOP" --emit-dir lg:"$W/impllg" --workdir "$W/c_impl" \
     --emit diagnostics:"$W/d_impl.jsonl" > "$W/impl.log" 2>&1
  [ "$(ngraphs $W/impllg)" -ge 1 ] && PRPGEN=PASS || PRPGEN=FAIL
fi

# 5. --reader yosys-slang -> lg (normalized; only if the gate could read it)
if [ "$GATE" = PASS ]; then
  (cd "$NORM" && $LHD compile --reader yosys-slang --top "$TOP" $FILES --emit-dir lg:"$W/yslg" \
     --workdir "$W/c_ys" --emit diagnostics:"$W/d_ys.jsonl" -- $YSREAD_LHD) > "$W/ys.log" 2>&1
  [ "$(ngraphs $W/yslg)" -ge 1 ] && LGYS=PASS || LGYS=FAIL
fi

# 6. lec prp-lg vs slang-lg  (lgyosys: reliable on lg-vs-lg, models 'x as don't-care)
if [ "$PRPGEN" = PASS ] && [ "$LGSLANG" = PASS ]; then
  timeout $LECT $LHD lec --impl lg:"$W/impllg" --ref lg:"$W/sllg" --top "$TOP" --workdir "$W/lec_sl" --set lec.solver=lgyosys > "$W/lec_sl.log" 2>&1
  LECSL=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lec_sl.log" | head -1); [ -z "$LECSL" ] && LECSL=TIMEOUT
fi
# 7. lec prp-lg vs yosys-slang-lg
if [ "$PRPGEN" = PASS ] && [ "$LGYS" = PASS ]; then
  timeout $LECT $LHD lec --impl lg:"$W/impllg" --ref lg:"$W/yslg" --top "$TOP" --workdir "$W/lec_ys" --set lec.solver=lgyosys > "$W/lec_ys.log" 2>&1
  LECYS=$(grep -oE "(PROVEN|REFUTED|UNKNOWN)" "$W/lec_ys.log" | head -1); [ -z "$LECYS" ] && LECYS=TIMEOUT
fi

# 8. abc gen (only when lec-vs-slang PROVEN): color acyclic then map.
if [ "$LECSL" = PROVEN ]; then
  cp -r "$W/impllg" "$W/abcin"
  timeout 120 $LHD pass color acyclic lg:"$W/abcin" > "$W/color.log" 2>&1
  timeout 200 $LHD pass abc lg:"$W/abcin" --set pass.abc.library="$ABCLIB" --emit-dir lg:"$W/abc" > "$W/abc.log" 2>&1
  [ "$(ngraphs $W/abc)" -ge 1 ] && ABC=PASS || { ABC=FAIL; MSG=$(emsg "$W/abc.log"); }
fi

# ── PRIMARY classify: the slang deliverable path (first failing stage).  The
# yosys-slang lg-gen / lec are CROSS-CHECK signals recorded separately (their own
# counters), so they never demote a module whose slang round-trip already PROVEN.
CAT=""
if   [ "$RSLANG" = FAIL ] && [ "$GATE" = FAIL ]; then CAT="yosys+slang fails"
elif [ "$RSLANG" = FAIL ]; then                       CAT="--reader slang fails"
elif [ "$LGSLANG" = FAIL ]; then                      CAT="lg gen from slang fails"
elif [ "$PRPGEN" = FAIL ]; then                       CAT="prp gen fails"; MSG=$(emsg "$W/d_impl.jsonl")
elif [ "$LECSL" = REFUTED ]; then                     CAT="lec prp vs from slang fails"; MSG=$(grep -oE 'counterexample[^"]*' $W/lec_sl.log|grep -v schema|head -1)
elif [ "$LECSL" = PROVEN ]; then                      CAT="PASS"
elif [ "$LECYS" = PROVEN ] && [ "$LECSL" != REFUTED ]; then CAT="PASS"   # slang lec inconclusive but yosys-slang proved
elif [ "$LECSL" = TIMEOUT ] || [ "$LECSL" = UNKNOWN ] || [ "$LECYS" = TIMEOUT ]; then CAT="lec inconclusive"
else                                                  CAT="other: rslang=$RSLANG lgslang=$LGSLANG prpgen=$PRPGEN lecsl=$LECSL lgys=$LGYS"
fi

# ── place artifacts ──────────────────────────────────────────────────────────
if [ "$CAT" = "PASS" ]; then
  cp "$PRP" "$HERE/pass/"; rm -f "$HERE/fail/$TOP.prp" "$HERE/fail/$TOP.md"
else
  rm -f "$HERE/pass/$TOP.prp"
  [ -f "$PRP" ] && cp "$PRP" "$HERE/fail/"
  {
    echo "# $TOP — $CAT"; echo
    echo "kind=$kind"; echo
    echo "| stage | result |"; echo "|---|---|"
    echo "| yosys+slang gate | $GATE |"
    echo "| --reader slang -> prp | $RSLANG |"
    echo "| slang -> lg | $LGSLANG |"
    echo "| prp -> lg | $PRPGEN |"
    echo "| yosys-slang -> lg | $LGYS |"
    echo "| lec prp vs slang | $LECSL |"
    echo "| lec prp vs yosys-slang | $LECYS |"
    echo "| abc gen | $ABC |"; echo
    echo "**First failure message:**"; echo '```'; echo "${MSG:-(none captured)}"; echo '```'
  } > "$HERE/fail/$TOP.md"
fi

# TSV: top  cat  kind  gate  rslang  lgslang  lgys  prpgen  lecsl  lecys  abc  msg
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
  "$TOP" "$CAT" "$kind" "$GATE" "$RSLANG" "$LGSLANG" "$LGYS" "$PRPGEN" "$LECSL" "$LECYS" "$ABC" "${MSG//$'\t'/ }" >> "$RESULTS"
echo "$TOP	$CAT	kind=$kind	gate=$GATE rslang=$RSLANG lgslang=$LGSLANG lgys=$LGYS prpgen=$PRPGEN lecsl=$LECSL lecys=$LECYS abc=$ABC"
