#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: compile.specialize_modules must keep the --top graph even when
# another unit of the same design instantiates it.
#
# The Pyrope reader lowers every unit, and the top is picked only after the
# graph pipeline (pick_top_graph). The specialize cleanup kept only what the
# UNINSTANTIATED graphs reach; once `outer` re-pointed its Sub at the
# constant clone `inner__k<hash>`, the original `inner` looked unreachable and
# was deleted, so `--top inner` failed with "compile: top 'inner' not found".
set -u
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_specialize_top_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat >"$W/d.prp" <<'EOF'
pub mod inner(en:Bool, d:U8) -> (y:U8@[]) {
  if en {
    y = d ^ 1
  } else {
    y = d
  }
}
pub mod outer(d:U8) -> (y:U8@[]) {
  mut u = inner::[name=u](en = false, d = d)
  y = u.y
}
EOF

# (1) --top names an instantiated, constant-configured child.
"$LHD" compile "$W/d.prp" --top inner --workdir "$W/w_inner" \
  --set compile.specialize_modules=true --emit-dir "verilog:$W/v_inner" -q >"$W/inner.log" 2>&1 \
  || { cat "$W/inner.log" >&2; fail "(1) --top inner was dropped by compile.specialize_modules"; }
[ -f "$W/v_inner/d.inner.v" ] || fail "(1) the original selected top d.inner was not emitted"
echo "PASS(1): an instantiated --top survives compile.specialize_modules"

# (2) The parent still uses its specialized clone; with --top outer the unused
# original is still cleaned up.
"$LHD" compile "$W/d.prp" --top outer --workdir "$W/w_outer" \
  --set compile.specialize_modules=true --emit-dir "verilog:$W/v_outer" -q >"$W/outer.log" 2>&1 \
  || { cat "$W/outer.log" >&2; fail "(2) --top outer did not compile"; }
grep -Eq '^inner__k[0-9a-f]+ ' "$W/v_outer/d.outer.v" || fail "(2) outer does not instantiate the specialized clone"
[ ! -f "$W/v_outer/d.inner.v" ] || fail "(2) the unreachable original inner was kept"
echo "PASS(2): the parent's clone is used and the unselected original is removed"
