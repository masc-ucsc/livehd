#!/usr/bin/env bash
# PRIVATE, EXPLICIT build of the fourteen canonical Compiler targets the D3
# probes load -- the step that the default `lake build` does NOT cover.
#
# WHY THIS EXISTS.  `lake build` with no target builds the library root's import
# closure (7 jobs here).  The fourteen `Compiler/*` modules in `CRITICAL_OLEANS`
# are NOT in that closure, so a default build succeeding -- and a default
# `--no-build` reporting "All targets up-to-date" -- says nothing about them.
# Measured in the private checkout: the explicit 14-target `lake --no-build
# build` exited 3 at `Compiler.DesignCert`, and still exited 3 at
# `Compiler.ResidualIR` after an unrelated 34-job Projection build.  Hash
# equality with an audited artifact set is PREPARATION; it is not a validated
# source/build closure, and neither is default-target success.
#
# WHAT IT REFUSES.  A build root that is a symlink, a dirty worktree, a
# `D3_ARTIFACT_DIR` in the environment, and -- the one that bit us -- a build
# into a context that still has LIVE IMPORTERS.  Writing `.olean`s under a root
# another process is importing from is exactly the shared-mutable-state hazard
# this private root exists to avoid; equal hashes afterwards do not prove the
# window was harmless.
#
# COST ACCOUNTING.  Dependency preparation and build cost are recorded here and
# belong to NO generation, simulation or readiness metric.
#
#   private_compiler_build.sh <private-checkout> <out-dir>
set -uo pipefail
P=${1:?private checkout root}; OUT=${2:?out dir}
PABS=$(cd "$P" && pwd -P) || { echo "REFUSING: cannot resolve $P"; exit 2; }
L=$PABS/formal/lean

TARGETS=(CompileDesignDefs CompileGraphDefs CompileOpDefs CompileDesign CompileGraph
         CompileOp D3Harness ReifyGen Reify ReifyProof Runtime DesignCert
         ResidualIR ResidualSemantics)
[ "${#TARGETS[@]}" = 14 ] || { echo "REFUSING: target list is ${#TARGETS[@]}, expected 14"; exit 2; }
MODS=(); for t in "${TARGETS[@]}"; do MODS+=("LeanSemanticPrimitives.Compiler.$t"); done
ARTDIR=$L/.lake/build/lib/lean/LeanSemanticPrimitives/Compiler

[ -d "$L" ] || { echo "REFUSING: no formal/lean at $L"; exit 2; }
[ -L "$L/.lake" ] && { echo "REFUSING: $L/.lake is a symlink -- that is a shared build-root alias"; exit 2; }
[ -n "${D3_ARTIFACT_DIR:-}" ] && { echo "REFUSING: D3_ARTIFACT_DIR is set; that seam changes what is hashed without changing what is loaded"; exit 2; }
if [ -n "$(git -C "$PABS" status --porcelain)" ]; then
  echo "REFUSING: $PABS is dirty; a build whose source nobody can regenerate is not evidence"; exit 2
fi

# LIVE IMPORTERS -- detected from what a process actually HOLDS, and from
# KNOWN OWNED HANDLES that must be confirmed terminal by identity, not by silence.
#
# Two generations of this guard were wrong in different ways.
#
# The first grepped `ps -eo args` for the private root: it MISSED a reader that
# found the root through `LEAN_PATH` or its working directory while its argv was
# relative, and it REFUSED an unrelated waiter whose command line merely named
# the path.
#
# The second walked each CANDIDATE's ancestors looking for this guard's pid and
# skipped any process that found it.  The comment claimed that excluded "my own
# ancestors"; it actually excluded every DESCENDANT of this shell -- so a real
# importer launched from the same session would have been silently skipped.  The
# exclusion is now exactly two pids: this shell and the inspection helper itself.
# Nothing is ever excluded for being a descendant.
#
# A process that exists but cannot be inspected is UNKNOWN, and unknown refuses.
# A proc-wide scan whose permission errors are swallowed is not evidence that no
# consumer remains.
#
# PCB_KNOWN_CONSUMERS="pid:starttime_ticks,..." names handles whose termination
# must be established by identity.  A live pid whose starttime matches is a live
# consumer; a live pid whose starttime differs is a RECYCLED pid and is reported
# as such rather than being mistaken for the consumer.
live=$(python3 -I - "$PABS" "$$" "${PCB_KNOWN_CONSUMERS:-}" "${PCB_ACK_UNINSPECTABLE:-}" <<'PYGUARD'
import os, sys, pathlib

root = pathlib.Path(sys.argv[1]).resolve()
shell_pid = int(sys.argv[2])
known = [h for h in sys.argv[3].replace(",", " ").split() if h]
# A process that exists but cannot be inspected is UNKNOWN and refuses.  An
# operator may acknowledge a SPECIFIC one, bound by pid AND starttime so the
# acknowledgement cannot silently drift to another process later.
ack = {}
for _h in (sys.argv[4] if len(sys.argv) > 4 else "").replace(",", " ").split():
    _p, _, _st = _h.partition(":")
    if _p.isdigit():
        ack[int(_p)] = _st
EXCLUDE = {os.getpid(), shell_pid}          # this helper and the shell that ran it. Nothing else.
MYUID = os.getuid()

def resolve(path, cwd=None):
    """Physically resolve, honouring a process's own cwd for a relative path."""
    try:
        q = pathlib.Path(path)
        if not q.is_absolute() and cwd is not None:
            q = pathlib.Path(cwd) / q
        return q.resolve()
    except Exception:
        return None

def under(path, cwd=None):
    r = resolve(path, cwd)
    try:    return r is not None and r.is_relative_to(root)
    except Exception: return False

def starttime(pid):
    st = pathlib.Path(f"/proc/{pid}/stat").read_text()
    return st[st.rindex(")") + 2:].split()[19]

hits, unknown, notes = [], [], []

# ---- known owned handles, checked by IDENTITY ----------------------------
for h in known:
    pid_s, _, want = h.partition(":")
    try:
        pid = int(pid_s)
    except ValueError:
        unknown.append(f"  malformed known handle {h!r}"); continue
    if not pathlib.Path(f"/proc/{pid}").exists():
        notes.append(f"  known consumer {pid} is gone (starttime {want or '?'})"); continue
    try:
        got = starttime(pid)
    except Exception as e:
        unknown.append(f"  known consumer {pid} exists but its starttime is unreadable ({e})"); continue
    if want and got != want:
        notes.append(f"  pid {pid} is RECYCLED (starttime {got} != {want}); the known consumer is gone")
    else:
        hits.append(f"{pid:>8}  KNOWN OWNED CONSUMER still live (starttime {got})")

# ---- possession scan over this user's processes --------------------------
for d in sorted(pathlib.Path("/proc").iterdir()):
    if not d.name.isdigit():
        continue
    pid = int(d.name)
    if pid in EXCLUDE:
        continue
    try:
        if d.stat().st_uid != MYUID:
            continue                      # another user's process; not ours to inspect
    except Exception:
        continue                          # vanished between listing and stat
    cwd = None
    try:    cwd = os.readlink(d / "cwd")
    except Exception: pass
    why, probed, failed = None, 0, 0
    try:
        for fd in (d / "fd").iterdir():
            probed += 1
            tgt = os.readlink(fd)
            # pipe:[N], socket:[N] and anon_inode:... are NOT filesystem paths;
            # joining one onto a cwd inside the root made every pipe a false hit.
            if not tgt.startswith("/"):
                continue
            if under(tgt, cwd):
                why = f"open fd -> {tgt}"; break
    except FileNotFoundError:
        continue                          # exited; nothing to decide
    except Exception:
        failed += 1
    if not why:
        try:
            probed += 1
            for line in (d / "maps").read_text().splitlines():
                f = line.split(" ", 5)[-1].strip()
                if f.startswith("/") and under(f, cwd):
                    why = f"mapped {f}"; break
        except FileNotFoundError:
            continue
        except Exception:
            failed += 1
    if not why:
        try:
            probed += 1
            for kv in (d / "environ").read_bytes().split(b"\0"):
                k, _, v = kv.decode("utf-8", "replace").partition("=")
                if k in ("LEAN_PATH", "D3_LEAN_ROOT", "LAKE_HOME", "LAKE_PKG_URL_MAP") \
                   and any(under(x, cwd) for x in v.split(":") if x):
                    why = f"{k} references the root"; break
        except FileNotFoundError:
            continue
        except Exception:
            failed += 1
    if not why and cwd is not None and under(cwd):
        why = f"cwd inside the root ({cwd})"
    try:    cmd = (d / "cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "replace")[:110]
    except Exception: cmd = "?"
    if why:
        hits.append(f"{pid:>8}  {why}\n          {cmd}")
    elif failed and failed == probed and pathlib.Path(f"/proc/{pid}").exists():
        # Every probe failed on a process that is still there: this is UNKNOWN,
        # not "no importer".
        try:    _st = starttime(pid)
        except Exception: _st = "?"
        if pid in ack and (not ack[pid] or ack[pid] == _st):
            notes.append(f"  ACKNOWLEDGED uninspectable: {pid} (starttime {_st}) {cmd}")
        else:
            unknown.append(f"{pid:>8}  UNINSPECTABLE ({failed}/{probed} probes failed, starttime {_st})"
                           f"\n          {cmd}"
                           f"\n          acknowledge with PCB_ACK_UNINSPECTABLE={pid}:{_st} if it is not a consumer")

for n in notes:
    print(n, file=sys.stderr)
print("\n".join(hits + unknown))
PYGUARD
)
if [ -n "$live" ]; then
  echo "REFUSING: this build context has live or uninspectable consumers; wait for a confirmed terminal result"
  echo "$live"
  exit 4
fi

mkdir "$OUT" 2>/dev/null || { echo "REFUSING: attempt root $OUT already exists"; exit 3; }
# The build runs in a subshell that cd's into the Lean root, so every output
# path must be ABSOLUTE.  A relative OUT silently sent GNU time's -o into the
# wrong directory, which surfaced -- correctly -- as `MISSING build producer
# status (wrapper rc=125)` rather than as a pass.
OUT=$(readlink -f "$OUT") || { echo "REFUSING: cannot resolve $OUT"; exit 2; }

# ---- identities BEFORE the build -----------------------------------------
{ echo "started_at=$(date -Iseconds)"
  echo "host=$(uname -n) kernel=$(uname -r) nproc=$(nproc)"
  echo "cwd=$L"
  echo "private_root=$PABS"
  echo "private_head=$(git -C "$PABS" rev-parse HEAD)"
  echo "private_status_clean=yes"
  echo "lean_toolchain=$(cat "$L/lean-toolchain")"
  echo "lake_version=$(cd "$L" && lake --version 2>&1 | head -1)"
  echo "lakefile_sha256=$(sha256sum "$L/lakefile.toml" | cut -d' ' -f1)"
  echo "lake_manifest_sha256=$(sha256sum "$L/lake-manifest.json" 2>/dev/null | cut -d' ' -f1)"
  echo "argv=lake build --no-cache ${MODS[*]}"
  for t in "${TARGETS[@]}"; do
    src=$L/LeanSemanticPrimitives/Compiler/$t.lean
    echo "source=$t sha256=$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1) bytes=$(stat -c%s "$src" 2>/dev/null)"
  done
} > "$OUT/BINDING.txt"
for t in "${TARGETS[@]}"; do
  f=$ARTDIR/$t.olean
  if [ -f "$f" ]; then echo "$t $(sha256sum "$f" | cut -d' ' -f1) $(stat -c%s "$f")"
  else echo "$t <absent> 0"; fi
done > "$OUT/artifacts.before"

# ---- the build (dependency-preparation cost, not a generation metric) -----
( cd "$L" && /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$OUT/build.time" \
    nice -n19 ionice -c3 env LEAN_NUM_THREADS=4 lake build --no-cache "${MODS[@]}" ) \
  > "$OUT/build.stdout" 2> "$OUT/build.stderr"
rcb=$?; echo "$rcb" > "$OUT/build.exit"
tb=$(awk '$1=="exit"{print $2; f=1} END{exit !f}' "$OUT/build.time" 2>/dev/null); tbr=$?
if [ "$tbr" != 0 ] || [ -z "$tb" ]; then
  echo "MISSING build producer status (wrapper rc=$rcb)" | tee "$OUT/DISPOSITION.txt"; exit 97; fi
[ "$tb" = "$rcb" ] || { echo "DISAGREE build wrapper=$rcb producer=$tb" | tee "$OUT/DISPOSITION.txt"; exit 96; }

for t in "${TARGETS[@]}"; do
  f=$ARTDIR/$t.olean
  if [ -f "$f" ]; then echo "$t $(sha256sum "$f" | cut -d' ' -f1) $(stat -c%s "$f")"
  else echo "$t <absent> 0"; fi
done > "$OUT/artifacts.after"
# Both identities are kept.  A changed artifact needs fresh evidence only for
# what it affects; nothing that already passed is blindly re-run.
diff "$OUT/artifacts.before" "$OUT/artifacts.after" > "$OUT/artifacts.diff"
chg=$(grep -c '^>' "$OUT/artifacts.diff" 2>/dev/null || echo 0)
echo "build rc=$rcb; $chg of 14 artifact identities changed"

[ "$rcb" = 0 ] || { echo "REFUSED build exit=$rcb" | tee "$OUT/DISPOSITION.txt"; exit "$rcb"; }

# ---- the required explicit 14-target freshness check ----------------------
( cd "$L" && /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$OUT/nobuild.time" \
    nice -n19 lake --no-build build "${MODS[@]}" ) \
  > "$OUT/nobuild.stdout" 2> "$OUT/nobuild.stderr"
rcn=$?; echo "$rcn" > "$OUT/nobuild.exit"
if [ "$rcn" != 0 ]; then
  echo "NOT VALIDATED: the explicit 14-target --no-build exited $rcn, not 0" | tee "$OUT/DISPOSITION.txt"
  grep -m3 'out-of-date\|Building' "$OUT/nobuild.stdout" 2>/dev/null
  exit "$rcn"
fi
echo "VALIDATED: build exit 0 and the explicit 14-target --no-build exit 0 ($chg/14 artifacts changed)" \
  | tee "$OUT/DISPOSITION.txt"
exit 0
