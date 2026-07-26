#!/usr/bin/env bash
# Import resolution precedence: what a plain `import("unit")` binds to when that
# same unit is ALSO supplied as a pre-built IR input.
#
# The contract, in three cases:
#   1. no ln:/lg: input   -> re-parse the sibling .prp into a fresh LNAST
#   2. ln: input          -> reuse that LNAST; do NOT re-parse the sibling
#   3. lg: input (with or without ln:) -> call the LGraph directly; neither
#      re-parse the source nor elaborate an LNAST for it. Same as any other
#      lgraph call site — there is nothing left to parse to build the top.
#
# What makes reuse observable: the ln: and lg: artifacts are built from a
# DIFFERENT definition of the same unit than the .prp sitting next to the
# importer. Each definition gives the output a distinct width, so the width the
# importer instantiates says which definition actually won:
#
#   sibling .prp on disk :  8 bits
#   the ln: artifact     : 16 bits
#   the lg: artifact     : 32 bits
#
# Each case also asserts on `--result-json` `inputs`, which lists every source
# the front-end actually read: the sibling appearing there IS the re-parse.
#
# Usage: ir_import_precedence_test.sh [case...]   (default: all four)
# Cases: no_ir ln_only lg_only ln_and_lg
set -u

if   [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd
elif [ -x ./lhd/lhd ];           then LHD=./lhd/lhd
else echo "FAIL: lhd binary not found"; exit 3; fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
rc=0
fail() { echo "FAIL: $*"; rc=1; }

mkdir -p "$W/main" "$W/altln" "$W/altlg"

# The importer, plus the sibling definition importer-directory discovery finds.
cat > "$W/main/lib_thing.prp" <<'EOF'
pub mod thing(a:u8) -> (o:u8@[0]) { o = a }
EOF
cat > "$W/main/use.prp" <<'EOF'
const lib_thing = import("lib_thing")
pub mod use_top(a:u8) -> (o:u32@[0]) { o = lib_thing.thing(a=a).o }
EOF

# Same unit name, deliberately different definitions, built into IR artifacts.
cat > "$W/altln/lib_thing.prp" <<'EOF'
pub mod thing(a:u8) -> (o:u16@[0]) { o = a }
EOF
cat > "$W/altlg/lib_thing.prp" <<'EOF'
pub mod thing(a:u8) -> (o:u32@[0]) { o = a }
EOF

$LHD compile "$W/altln/lib_thing.prp" --emit-dir "ln:$W/LN" --workdir "$W/w_ln" -q \
  || { echo "FAIL: could not build the ln: artifact"; exit 3; }
$LHD compile "$W/altlg/lib_thing.prp" --emit-dir "lg:$W/LG" --workdir "$W/w_lg" -q \
  || { echo "FAIL: could not build the lg: artifact"; exit 3; }

# width of `lib_thing.thing`'s output pin in the emitted library == which
# definition the importer ended up instantiating
thing_width() {
  awk '/^graph_io .*lib_thing\.thing$/ {inside=1; next}
       /^graph_io /                    {inside=0}
       inside && /^  output /          {for (i=1;i<=NF;i++) if ($i ~ /^bits=/) {sub("bits=","",$i); print $i; exit}}' \
    "$1/library.txt"
}

# run_case NAME WANT_WIDTH WANT_REPARSE <extra lhd inputs...>
run_case() {
  local name=$1 want=$2 want_reparse=$3
  shift 3
  local out="$W/out_$name" rj="$W/rj_$name.json"
  rm -rf "$out"
  if ! $LHD compile "$W/main/use.prp" "$@" --emit-dir "lg:$out" \
         --workdir "$W/w_$name" --result-json "$rj" -q; then
    fail "$name: compile failed"
    return
  fi

  local got
  got=$(thing_width "$out")
  if [ "$got" = "$want" ]; then
    echo "ok: $name -> thing.o is ${got} bits"
  else
    fail "$name: expected thing.o to be ${want} bits, got '${got:-<no lib_thing.thing graph>}'"
  fi

  # Did the front-end read the sibling source? Only case 1 may.
  local reparsed=no
  grep -q "main/lib_thing\.prp" "$rj" && reparsed=yes
  if [ "$reparsed" = "$want_reparse" ]; then
    echo "ok: $name -> sibling .prp re-parsed: $reparsed"
  else
    fail "$name: sibling .prp re-parsed=$reparsed, expected $want_reparse"
  fi
}

cases=("$@")
[ ${#cases[@]} -eq 0 ] && cases=(no_ir ln_only lg_only ln_and_lg)
for c in "${cases[@]}"; do
  case $c in
  no_ir)     run_case no_ir      8  yes ;;
  ln_only)   run_case ln_only   16  no  "ln:$W/LN" ;;
  lg_only)   run_case lg_only   32  no  "lg:$W/LG" ;;
  ln_and_lg) run_case ln_and_lg 32  no  "ln:$W/LN" "lg:$W/LG" ;;
  *) echo "FAIL: unknown case '$c'"; exit 3 ;;
  esac
done

exit $rc
