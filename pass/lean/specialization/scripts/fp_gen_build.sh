#!/usr/bin/env bash
# Build the GENERATION-ONLY driver twice, into ISOLATED project-local roots:
#   master side -> master's formal/lean + gen_probe.master.lean
#   pin side    -> the extracted 9bb41a09d tree + gen_probe.pin.lean
# Same Lean 4.31.0 (both trees pin it), same leanc, same -O2.
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"   # the repo root, from this script's location
G=$R/generated/master_port/fp/genprobe
export LEAN_NUM_THREADS=4

# The two trees name the driver folder differently: master's is
# Futamura_Projection_Scripts/, the 9bb41a09d pin's is scripts/.
build() {                       # build <tag> <tree-formal-lean> <driver-dir> <probe>
  local tag=$1 tree=$2 dir=$3 probe=$4
  local root=$G/root-$tag
  mkdir -p "$root"
  echo "OPT=-O2 ROOT=$root bash $dir/build-opt.sh $probe   # cwd=$tree" > "$root/build.cmd"
  ( cd "$tree" && /usr/bin/time -f '%e s %M kB' -o "$root/build.time" \
      nice -n19 ionice -c3 env OPT=-O2 ROOT="$root" bash "$dir/build-opt.sh" "$probe" ) \
      > "$root/build.log" 2>&1
  local rc=$?
  echo "$tag rc=$rc $(cat "$root/build.time" 2>/dev/null)"
  [ $rc -ne 0 ] && tail -4 "$root/build.log"
  [ -x "$root/nat/$(basename "$probe" .lean)" ] && sha256sum "$root/nat/$(basename "$probe" .lean)"
}

build master "$R/formal/lean"           Futamura_Projection_Scripts Futamura_Projection_Scripts/gen_probe.lean
build pin    "$G/pin_tree/formal/lean"  scripts                     scripts/gen_probe.lean
echo "GENPROBE BUILDS DONE"
