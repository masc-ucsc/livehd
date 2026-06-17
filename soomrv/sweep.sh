#!/bin/bash
# sweep.sh — drive translate.sh across all soomrv modules and build record.html.
#   sweep.sh [-P N] [module ...]   (default: all soomrv/modules.txt, -P 6)
set -u
HERE=/mada/users/renau/projs/livehd/soomrv
ORIG=/mada/users/renau/projs/soomrv/repo
NORM=/tmp/snorm
PAR=6
[ "${1:-}" = "-P" ] && { PAR=$2; shift 2; }

# 1. file list (relative to the repo, Makefile SRC_FILES order)
python3 - "$ORIG" <<'PY' > /tmp/soomrv_rel.txt
import re,os,sys
repo=sys.argv[1]; mk=open(os.path.join(repo,"Makefile")).read()
m=re.search(r'SRC_FILES\s*=\s*(.*?)\n\.PHONY', mk, re.S)
files=[f.strip().rstrip('\\').strip() for f in m.group(1).split('\n')]
print(' '.join(f for f in files if f and not f.startswith('#') and os.path.exists(os.path.join(repo,f))))
PY

# 2. normalized tree for the yosys+slang path (!& -> ~&, !| -> ~|, !^ -> ~^;
#    yosys-slang's bundled slang lacks the chained-unary parse).
rm -rf "$NORM"; cp -r "$ORIG" "$NORM"
find "$NORM/src" "$NORM/hardfloat" -name '*.sv' -o -name '*.v' 2>/dev/null | while read f; do
  perl -pi -e 's/!&/~&/g; s/!\|/~|/g; s/!\^/~^/g' "$f"
done

# 3. per-module sweep
RES=/tmp/soomrv_results.tsv; : > "$RES"
MODS=${@:-$(cat "$HERE/modules.txt")}
export RESULTS="$RES"
printf '%s\n' $MODS | xargs -P "$PAR" -I{} bash "$HERE/translate.sh" {} > /tmp/soomrv_sweep.out 2>&1
sort -o "$RES" "$RES"
echo "sweep done: $(wc -l < $RES) modules"
awk -F'\t' '{print $2}' "$RES" | sed 's/:.*//' | sort | uniq -c | sort -rn

# 4. report
python3 "$HERE/gen_report.py" "$RES" "$HERE/record.html"
echo "wrote $HERE/record.html"
