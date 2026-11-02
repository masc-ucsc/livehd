#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=${LHD:-lhd/lhd}
W=${TEST_TMPDIR:-$(mktemp -d)}
mkdir -p "$W/src"
cat > "$W/src/child.prp" <<'EOF'
pub mod child(clk:Clock, a:U8, k:U1) -> (y:U8@[1]) {
  reg q:U8 = 0
  q = a ^ k
  y = q
}
EOF
cat > "$W/src/top.prp" <<'EOF'
const child = import("child.child")
pub mod top(clk:Clock, a:U8) -> (y:U8@[1]) {
  const c = child(clk=clk, a=a, k=0)
  y = c.y
}
EOF
compile() {
  "$LHD" compile "$W/src/top.prp" --top top --emit-dir "lg:$W/$1" \
    --workdir "$W/$2" --result-json "$W/$1.json" "${@:3}"
}
compile before warm_work
python3 - "$W/src/top.prp" <<'PYEDIT'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(p.read_text().replace("y = c.y", "y = c.y ^ 1"))
PYEDIT
compile edited warm_work
compile cold cold_work --set lhd.incremental=false
[ "$("$LHD" tool diff "lg:$W/cold" "lg:$W/edited" --structural -q)" = identical ]
[ "$("$LHD" tool diff "lg:$W/before" "lg:$W/edited" --structural -q)" != identical ]
python3 - "$W/edited.json" <<'PYCHECK'
import json
import sys
v = json.load(open(sys.argv[1]))["incremental"]["compile"]
assert v["hits"] > 0, v
assert v["refused"] == 0 and v["store_failed"] == 0, v
PYCHECK
# A comment-only followup must still restore the corrected generation.
printf '\n// unchanged circuit\n' >> "$W/src/top.prp"
compile comment warm_work
[ "$("$LHD" tool diff "lg:$W/cold" "lg:$W/comment" --structural -q)" = identical ]
# A retained caller still owns the old specialized child when an unrelated
# caller edit rebuilds the clean child's original interface.
mkdir -p "$W/retained/src"
cp "$W/src/child.prp" "$W/retained/src/child.prp"
cat > "$W/retained/src/sibling.prp" <<'EOF'
const child = import("child.child")
pub mod sibling(clk:Clock, a:U8) -> (y:U8@[1]) {
  const c = child(clk=clk, a=a, k=0)
  y = c.y
}
EOF
cat > "$W/retained/src/top.prp" <<'EOF'
const sibling = import("sibling.sibling")
pub mod top(clk:Clock, a:U8) -> (y:U8@[1]) {
  const c = sibling(clk=clk, a=a)
  y = c.y
}
EOF
W="$W/retained"
compile before warm_work
python3 - "$W/src/top.prp" <<'PYEDIT'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(p.read_text().replace("y = c.y", "y = c.y ^ 1"))
PYEDIT
compile edited warm_work
compile cold cold_work --set lhd.incremental=false
[ "$("$LHD" tool diff "lg:$W/cold" "lg:$W/edited" --structural -q)" = identical ]
"$LHD" lec --impl "lg:$W/edited" --ref "lg:$W/cold" --top top \
  --workdir "$W/proof" --result-json "$W/proof.json" --set formal.timeout=5 --set formal.engine=ind
python3 - "$W/proof.json" <<'PYCHECK'
import json
import sys
r = json.load(open(sys.argv[1]))
assert r["lec"]["verdict"] == "proven-full", r
PYCHECK
echo "PASS: specialized callee survives incremental caller edits"
