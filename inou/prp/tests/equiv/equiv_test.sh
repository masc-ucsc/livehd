#!/bin/bash
# Run a single Pyrope/Verilog equivalence test:
#   1. Lower <name>.prp through inou.prp |> pass.upass |> pass.lnast_to_lgraph
#      |> inou.cgen.verilog into a fresh temp lgdb / output dir.
#   2. Compare the generated <name>.v against the reference <name>.v with
#      inou/yosys/lgcheck.
#
# Usage: equiv_test.sh <name>
#   <name>: basename of the .prp/.v pair under inou/prp/tests/equiv/
#
# Required env / tools (resolved in order):
#   LGSHELL       - explicit path to lgshell
#   bazel-bin/main/lgshell, lgshell.runfiles/livehd/main/lgshell, $PATH
#   YOSYS         - explicit path to yosys2 binary (forwarded to lgcheck)
#
# The script is intentionally self-contained so it works both when invoked
# directly from the workspace root and from a Bazel sh_test sandbox.

set -euo pipefail

NAME="${1:-}"
if [ -z "${NAME}" ]; then
  echo "usage: $0 <name>" >&2
  exit 2
fi

# Resolve paths relative to the directory the script lives in so it works
# regardless of invocation cwd.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PRP_FILE="${SCRIPT_DIR}/${NAME}.prp"
REF_VLOG="${SCRIPT_DIR}/${NAME}.v"

if [ ! -f "${PRP_FILE}" ]; then
  echo "error: ${PRP_FILE} not found" >&2
  exit 2
fi
if [ ! -f "${REF_VLOG}" ]; then
  echo "error: ${REF_VLOG} not found" >&2
  exit 2
fi

# Locate lgshell.
LGSHELL_BIN="${LGSHELL:-}"
if [ -z "${LGSHELL_BIN}" ]; then
  for c in \
    "$(pwd)/bazel-bin/main/lgshell" \
    "$(pwd)/lgshell" \
    "${SCRIPT_DIR}/../../../../bazel-bin/main/lgshell"; do
    if [ -x "$c" ]; then
      LGSHELL_BIN="$c"
      break
    fi
  done
fi
if [ -z "${LGSHELL_BIN}" ] || [ ! -x "${LGSHELL_BIN}" ]; then
  LGSHELL_BIN="$(command -v lgshell || true)"
fi
if [ -z "${LGSHELL_BIN}" ] || [ ! -x "${LGSHELL_BIN}" ]; then
  echo "error: cannot locate lgshell — set LGSHELL or build //main:lgshell" >&2
  exit 3
fi

# Locate lgcheck.
LGCHECK="${LGCHECK:-}"
if [ -z "${LGCHECK}" ]; then
  for c in \
    "$(pwd)/inou/yosys/lgcheck" \
    "${SCRIPT_DIR}/../../../yosys/lgcheck"; do
    if [ -x "$c" ]; then
      LGCHECK="$c"
      break
    fi
  done
fi
if [ -z "${LGCHECK}" ] || [ ! -x "${LGCHECK}" ]; then
  echo "error: cannot locate inou/yosys/lgcheck" >&2
  exit 3
fi

# Build a fresh workdir per run so prior runs never leak state.
WORK_DIR="$(mktemp -d -t equiv_${NAME}_XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT
LGDB="${WORK_DIR}/lgdb"
ODIR="${WORK_DIR}/out"
mkdir -p "${ODIR}"

# Pipeline: parse Pyrope -> upass (constprop + SSA normalisation + attributes,
# no verifier) -> lower to LGraph -> emit Verilog.
# ssa:1    harvests I/O metadata into tree_io, expands I/O tuple_add nodes
# attributes:1  runs Pyrope attribute propagation (sticky + comptime)
PIPELINE="inou.prp files:${PRP_FILE} \
  |> pass.upass constprop:1 verifier:false max_iters:1 ssa:1 attributes:1 \
  |> pass.lnast_to_lgraph path:${LGDB} \
  |> inou.cgen.verilog odir:${ODIR}"

echo ">>> ${LGSHELL_BIN} \"${PIPELINE}\""
"${LGSHELL_BIN}" "${PIPELINE}"

# Locate the produced Verilog.  upass/func_extract names extracted modules
# `<file>.<func>` (e.g. `trivial.trivial`), and cgen.verilog also drops a
# nearly-empty `<file>.v` wrapper.  Prefer the `<name>.<name>.v` file when
# both exist; otherwise take any single .v that isn't the empty wrapper.
GEN_VLOG=""
if [ -f "${ODIR}/${NAME}.${NAME}.v" ]; then
  GEN_VLOG="${ODIR}/${NAME}.${NAME}.v"
elif [ -f "${ODIR}/${NAME}.v" ]; then
  GEN_VLOG="${ODIR}/${NAME}.v"
else
  GEN_VLOG="$(find "${ODIR}" -maxdepth 2 -type f -name '*.v' | head -n 1)"
fi
if [ -z "${GEN_VLOG}" ] || [ ! -f "${GEN_VLOG}" ]; then
  echo "error: no Verilog produced under ${ODIR}" >&2
  ls -la "${ODIR}" >&2 || true
  exit 1
fi

# Rename module identifier from `<name>.<name>` (escape-id) to `<name>` so
# lgcheck/yosys see the same top as the reference.  The cgen output uses
# `\<name>.<name> ` (Verilog escape-id with trailing space) for the module
# header *and* for any internal references; replace both forms.
NORM_VLOG="${WORK_DIR}/${NAME}.normalized.v"
sed -e "s/\\\\${NAME}\\.${NAME} /${NAME} /g" \
    -e "s/\\\\${NAME}\\.${NAME}/${NAME}/g" \
    "${GEN_VLOG}" > "${NORM_VLOG}"

echo ">>> generated Verilog: ${GEN_VLOG}"
echo ">>> normalized Verilog: ${NORM_VLOG}"

# Run equivalence check.  lgcheck looks for bazel-bin/inou/yosys/yosys2
# relative to cwd; on the VM the bazel-bin symlink is missing (sandboxed
# build), so forward YOSYS explicitly via --yosys when set.
LGCHECK_ARGS=(--reference "${REF_VLOG}" --implementation "${NORM_VLOG}" --top "${NAME}")
if [ -n "${YOSYS:-}" ] && [ -x "${YOSYS}" ]; then
  LGCHECK_ARGS+=(--yosys "${YOSYS}")
fi
"${LGCHECK}" "${LGCHECK_ARGS[@]}"
