#!/usr/bin/env bash
# Controls for make_sim_launcher.py.
#
# The generator redirects a certificate's `CompileDesign` import to
# `DirectTrace` and cuts the residual half away.  If it cannot find the cut
# boundary it must REFUSE -- the alternative is a module importing DirectTrace
# while still containing code that needs the compiler, reported as a success.
#
# Each case runs THE SCRIPT, via a HARD LINK into a throwaway tree: same inode,
# so the control can never drift from the real generator, but its own path, so
# `realpath(__file__/../../../formal/lean)` lands on the fixture's lakefile.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
GEN="$ROOT/pass/lean/scripts/make_sim_launcher.py"
WORK="$ROOT/generated/master_port/sim_launcher_controls"
SRC="${1:-$ROOT/generated/master_port/replay/jobs/CORE-ET/verified_compiler/txfma_e4/lean/txfma_e4_Lgraph.lean}"

if [ ! -f "$SRC" ]; then
  echo "REFUSE: no certificate at $SRC (pass one as \$1)" >&2; exit 2
fi
fails=0

# fixture NAME [--no-sims]  ->  prints the fake tree root
fixture() {
  local name="$1"
  local nosims="${2:-}"
  local d="$WORK/$name"
  rm -rf "$d"; mkdir -p "$d/pass/lean/scripts" "$d/formal/lean"
  # Hard links: same inode, so a control can never run a stale copy. Both
  # files, because the generator imports its sibling -- and the first run after
  # that import was added failed here with ModuleNotFoundError, which is the
  # fixture doing its job.
  ln "$GEN" "$d/pass/lean/scripts/make_sim_launcher.py"
  ln "$ROOT/pass/lean/scripts/cert_cut.py" "$d/pass/lean/scripts/cert_cut.py"
  if [ "$nosims" = "--no-sims" ]; then
    printf 'name = "x"\n\n[[lean_lib]]\nname = "LeanSemanticPrimitives"\n' > "$d/formal/lean/lakefile.toml"
  else
    printf 'name = "x"\n\n[[lean_lib]]\nname = "Sims"\nglobs = ["Sims.+"]\n' > "$d/formal/lean/lakefile.toml"
  fi
  echo "$d"
}

run_case() { # name want_exit tree [extra args...]
  local name="$1" want="$2" d="$3"; shift 3
  python3 -I "$d/pass/lean/scripts/make_sim_launcher.py" "$@" > "$d/out.log" 2>&1
  local got=$?
  if [ "$got" -ne "$want" ]; then
    echo "MISS: $name -> exit $got, wanted $want"; sed -n '1,6p' "$d/out.log"; fails=$((fails+1)); return 1
  fi
  echo -n "ok: $name -> exit $got"
  return 0
}

mutate() { # out  python-expr-over-s
  python3 -I -c "
import sys, re
s = open(sys.argv[1]).read()
$2
open(sys.argv[2],'w').write(s)" "$SRC" "$1"
}

# ---- 1. a real certificate ------------------------------------------------
d=$(fixture accepts)
if run_case accepts 0 "$d" "$SRC"; then
  cert="$d/formal/lean/Sims/txfma_e4_Cert.lean"
  main="$d/formal/lean/Sims/txfma_e4_Main.lean"
  if [ ! -f "$cert" ] || [ ! -f "$main" ]; then
    echo " -- MISS: outputs missing"; fails=$((fails+1))
  elif grep -q 'Compiler.CompileDesign' "$cert"; then
    echo " -- MISS: the CompileDesign import survived"; fails=$((fails+1))
  elif ! grep -q 'txfma_e4_designCert' "$cert"; then
    echo " -- MISS: the certificate itself was cut away"; fails=$((fails+1))
  elif grep -q 'Compile-and-run' "$cert"; then
    echo " -- MISS: the residual half survived"; fails=$((fails+1))
  elif ! grep -q 'name = "Simtxfma_e4"' "$d/formal/lean/lakefile.toml"; then
    echo " -- MISS: no lean_exe stanza"; fails=$((fails+1))
  else
    echo ", cert cut, compiler import gone, stanza appended"
  fi
fi

# ---- 2. the stanza is appended ONCE ---------------------------------------
d=$(fixture idempotent)
python3 -I "$d/pass/lean/scripts/make_sim_launcher.py" "$SRC" >/dev/null 2>&1
python3 -I "$d/pass/lean/scripts/make_sim_launcher.py" "$SRC" >/dev/null 2>&1
n=$(grep -c 'name = "Simtxfma_e4"' "$d/formal/lean/lakefile.toml")
if [ "$n" -eq 1 ]; then echo "ok: idempotent -> 1 stanza after two runs"
else echo "MISS: idempotent -> $n stanzas"; fails=$((fails+1)); fi

# ---- 3. no cut boundary -> refuse, and leave NOTHING behind ---------------
d=$(fixture no_boundary)
mutate "$d/cut.lean" "
s = s.replace('/-- Compile-and-run.', '/-- removed.')
s = re.sub(r'^def (\w+)_step\s*:', r'def \1_stepX :', s, flags=re.M)"
if run_case no_boundary 1 "$d" "$d/cut.lean"; then
  if [ -d "$d/formal/lean/Sims" ]; then
    echo " -- MISS: refused but wrote $(ls "$d/formal/lean/Sims" | tr '\n' ' ')"; fails=$((fails+1))
  elif ! grep -q 'cannot locate the residual half' "$d/out.log"; then
    echo " -- MISS: refused for the wrong reason"; fails=$((fails+1))
  else
    echo ", nothing written"
  fi
fi

# ---- 4. the `_step` fallback still works when the marker alone is gone ----
d=$(fixture step_fallback)
mutate "$d/cut.lean" "s = s.replace('/-- Compile-and-run.', '/-- removed.')"
if grep -qE '^def \w+_step[[:space:]]*:' "$d/cut.lean"; then
  if run_case step_fallback 0 "$d" "$d/cut.lean"; then
    c="$d/formal/lean/Sims/txfma_e4_Cert.lean"
    if grep -q 'Compile-and-run\|removed\.' "$c"; then
      echo " -- MISS: the fallback did not cut the residual half"; fails=$((fails+1))
    elif [ "$(tail -c 3 "$c" | tr -d '\n')" = "-/" ]; then
      # Lean rejects a doc comment with nothing after it.  The fallback used to
      # cut AT the `def` line, leaving the `_step` doc comment dangling at the
      # end of the module -- and reporting success.
      echo " -- MISS: the module ends in a dangling comment"; fails=$((fails+1))
    elif [ "$(grep -c '/-' "$c")" != "$(grep -c -- '-/' "$c")" ]; then
      echo " -- MISS: unbalanced comment delimiters"; fails=$((fails+1))
    else echo ", cut above the _step doc comment"; fi
  fi
else
  echo "SKIP: step_fallback -- this certificate has no _step definition to fall back to"
fi

# ---- 5. no lakefile `Sims` lib -> refuse BEFORE writing -------------------
d=$(fixture no_sims_lib --no-sims)
if run_case no_sims_lib 1 "$d" "$SRC"; then
  if [ -d "$d/formal/lean/Sims" ]; then
    echo " -- MISS: refused but left $(ls "$d/formal/lean/Sims" | tr '\n' ' ') behind"; fails=$((fails+1))
  else echo ", nothing written"; fi
fi

# ---- 6. not a certificate at all ------------------------------------------
d=$(fixture no_cert)
printf 'def hello : Nat := 1\n' > "$d/cut.lean"
run_case no_cert 1 "$d" "$d/cut.lean" && echo ", no _designCert"

# ---- 7. --keep-compiler takes the file verbatim ---------------------------
d=$(fixture keep_compiler)
if run_case keep_compiler 0 "$d" --keep-compiler "$SRC"; then
  if ! diff -q "$SRC" "$d/formal/lean/Sims/txfma_e4_Cert.lean" >/dev/null; then
    echo " -- MISS: --keep-compiler altered the certificate"; fails=$((fails+1))
  else echo ", certificate byte-identical"; fi
fi

# ---- 8. --bench emits the differential harness ----------------------------
d=$(fixture bench)
if run_case bench 0 "$d" --bench "$SRC"; then
  if [ ! -f "$d/formal/lean/Sims/txfma_e4_Bench.lean" ]; then
    echo " -- MISS: no _Bench.lean"; fails=$((fails+1))
  elif ! grep -q 'name = "Benchtxfma_e4"' "$d/formal/lean/lakefile.toml"; then
    echo " -- MISS: no bench stanza"; fails=$((fails+1))
  else echo ", bench harness and stanza"; fi
fi

if [ "$fails" -eq 0 ]; then
  echo "PASS: a launcher is emitted only when the cut boundary is found"
else
  echo "FAIL: $fails control(s) missed"
fi
exit $((fails > 0))
