#!/usr/bin/env python3
"""Reconcile a fresh CORE-ET 122 run against the accepted 83/122 baseline.

WHAT RECONCILED MEANS HERE. Schema completeness, NOT coverage: 122 unique rows
in the canonical declared order, 18 fields each, every candidate RAN row bound
to a certificate whose hash matches and gated by all three gates. Coverage is
reported separately and is always the INTERSECTION of the runtime row and the
gates -- emission alone is not coverage, and a runtime row alone is not either.

Every verdict TRANSITION is printed by name. A fresh run that merely reports a
better total without saying which blocks moved, and why, is not reconciled.

Usage: coreet_run_reconcile.py <new_rows> <new_gates> <base_rows> <base_gates>
                               <module_list> [out.txt]
"""
import io
import sys


ROW_FIELDS = 18
ROW_REQUIRED = ("module", "emit_status", "parse_status", "compiles_ok",
                "runtime_status", "diagnostic", "cert_sha256")

# THE EXACT GATE SCHEMAS, pinned by name AND order. Checking only that a few
# columns exist is not enough: `cert_hash_matches_row` is the certificate
# AUTHENTICATION evidence, and deleting that one column used to leave a table
# that still reconciled and still reported its coverage -- because the outcome
# function defaulted the missing value to "yes". Missing authentication
# evidence must never default to success, so the whole column is mandatory.
GATE_HEADER_RUN = ["module", "emit_status", "runtime_status", "vc_gates", "cert_invariant",
                   "cert_differential", "cert_sha256", "verilog_sha256", "lean_sha256",
                   "cert_hash_matches_row", "first_failure"]
# The baseline gates were generated before `emit_status`/`runtime_status` were
# carried in the table; its canonical shape is its own, and equally exact.
GATE_HEADER_BASELINE = ["module", "emit_rc", "vc_gates", "cert_invariant", "cert_differential",
                        "cert_sha256", "verilog_sha256", "lean_sha256",
                        "cert_hash_matches_row", "first_failure"]


def read_tsv(path, label, problems, expect_fields=None, required=(), exact_header=None):
    """Raw-validate EVERY line before anything is converted to a dict.

    `dict(zip(header, fields))` is silently lossy: a 19-field row against an
    18-field header drops the extra field and then reports "18 on every row".
    So the field COUNT is checked on the raw split, against the header and
    against the declared contract, before a dict exists at all.
    """
    rows, hdr, lineno = [], None, 0
    try:
        fh = io.open(path, encoding="utf-8", errors="replace")
    except OSError as e:
        problems.append("%s: cannot be read (%s)" % (label, e))
        return [], []
    with fh:
        for line in fh:
            lineno += 1
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            f = line.split("\t")
            if hdr is None:
                hdr = f
                if expect_fields is not None and len(hdr) != expect_fields:
                    problems.append("%s: header has %d fields, the contract declares %d"
                                    % (label, len(hdr), expect_fields))
                for col in required:
                    if col not in hdr:
                        problems.append("%s: header is missing the required column %r" % (label, col))
                if exact_header is not None and hdr != exact_header:
                    missing = [c for c in exact_header if c not in hdr]
                    extra = [c for c in hdr if c not in exact_header]
                    detail = []
                    if missing:
                        detail.append("missing %s" % ", ".join(missing))
                    if extra:
                        detail.append("unexpected %s" % ", ".join(extra))
                    if not detail:
                        detail.append("columns out of canonical order")
                    problems.append("%s: header is not the canonical %d-column schema (%s)"
                                    % (label, len(exact_header), "; ".join(detail)))
                continue
            if len(f) != len(hdr):
                problems.append("%s line %d: %d fields against a %d-field header"
                                % (label, lineno, len(f), len(hdr)))
                continue
            if expect_fields is not None and len(f) != expect_fields:
                problems.append("%s line %d: %d fields, the contract declares %d"
                                % (label, lineno, len(f), expect_fields))
                continue
            rows.append(dict(zip(hdr, f)))
    if hdr is None:
        problems.append("%s: no header row" % label)
    return hdr or [], rows


def is_candidate(row):
    """A row that CLAIMS runtime coverage, and so owes a gate result."""
    return (row.get("emit_status") == "EMITTED" and row.get("parse_status") == "PARSED"
            and row.get("compiles_ok") == "true" and row.get("runtime_status") == "RAN")


def check_row_identity(rows, declared, label, problems):
    """Runtime rows must BE the declared corpus: same modules, once each, in order.

    A field-count check cannot see a substitution. Replacing one ungated row
    with a duplicate of another preserved 122 rows of 18 fields and silently
    fabricated a transition for the module that had been dropped, because the
    missing module was then read as "absent" from the baseline.
    """
    mods = [r.get("module", "") for r in rows]
    if len(mods) != len(declared):
        problems.append("%s: %d rows for %d declared modules" % (label, len(mods), len(declared)))
    dup = sorted({m for m in mods if mods.count(m) > 1})
    if dup:
        problems.append("%s: duplicate module row(s) for %s" % (label, ", ".join(dup[:5])))
    dset, mset = set(declared), set(mods)
    omitted = sorted(dset - mset)
    if omitted:
        problems.append("%s: declared module(s) with no row: %s" % (label, ", ".join(omitted[:5])))
    unknown = sorted(mset - dset)
    if unknown:
        problems.append("%s: row(s) for module(s) not in the declared list: %s"
                        % (label, ", ".join(unknown[:5])))
    if not dup and not omitted and not unknown and mods != declared:
        first = next((i for i, (a, b) in enumerate(zip(mods, declared)) if a != b), None)
        problems.append("%s: module sequence diverges from the declared list%s"
                        % (label, "" if first is None else " at row %d: %r vs %r"
                           % (first + 1, mods[first], declared[first])))


def check_gate_identity(rows, gates, label, problems, strict_order, exact_candidate_set=False):
    """A gate table must be one-to-one with the runtime rows it is read against.

    A missing, duplicate, unknown or reordered gate row is INFRASTRUCTURE
    INCOMPLETE, not a block-level outcome: silently turning it into "this block
    has no gate" would let a deleted row look like an honest non-acceptance.
    """
    gmods = [g.get("module", "") for g in gates]
    rmods = [r.get("module", "") for r in rows]
    rset = set(rmods)
    dup = sorted({m for m in gmods if gmods.count(m) > 1})
    if dup:
        problems.append("%s: duplicate gate row(s) for %s" % (label, ", ".join(dup[:5])))
    unknown = sorted({m for m in gmods if m not in rset})
    if unknown:
        problems.append("%s: gate row(s) for module(s) absent from the runtime rows: %s"
                        % (label, ", ".join(unknown[:5])))
    gset = set(gmods)
    uncovered = [r["module"] for r in rows if is_candidate(r) and r["module"] not in gset]
    if uncovered:
        problems.append("%s: %d candidate RAN row(s) have no gate row: %s"
                        % (label, len(uncovered), ", ".join(uncovered[:5])))
    if strict_order:
        if gmods != rmods:
            first = next((i for i, (a, b) in enumerate(zip(gmods, rmods)) if a != b), None)
            problems.append("%s: gate module sequence is not identical to the runtime rows%s"
                            % (label, "" if first is None else " (first divergence at row %d: %r vs %r)"
                               % (first + 1, gmods[first], rmods[first])))
    else:
        # A subset table (the baseline gates only cover its RAN blocks) must
        # still appear in the SAME relative order as the rows.
        order = {m: i for i, m in enumerate(rmods)}
        seq = [order[m] for m in gmods if m in order]
        if seq != sorted(seq):
            problems.append("%s: gate rows are not in the runtime rows' relative order" % label)
    if exact_candidate_set:
        cands = {r["module"] for r in rows if is_candidate(r)}
        extra = sorted(gset - cands)
        if extra:
            problems.append("%s: gate row(s) for %d non-candidate module(s): %s"
                            % (label, len(extra), ", ".join(extra[:5])))


def outcome(row, gate):
    """The LAST stage this block actually reached, never a flattering one."""
    emit = row.get("emit_status", "")
    diag = row.get("diagnostic", "")
    if emit == "NO_VERDICT":
        return "no-verdict"
    if emit == "NO_WRAPPER":
        return "no-wrapper"
    if emit == "EMIT_FAILED":
        return "emission-timeout" if "emission timeout" in diag else "front-end-failure"
    if emit == "NO_CERT":
        if diag.startswith("resource-limit:"):
            return "resource-limit"
        if "emission timeout" in diag:
            return "emission-timeout"
        return "front-end-failure"
    if row.get("parse_status") != "PARSED" or row.get("compiles_ok") != "true" \
       or row.get("runtime_status") != "RAN":
        return "not-run"
    if gate is None:
        # Unreachable once check_gate_identity has run: a candidate row with no
        # gate row makes the whole reconciliation fail before this is called.
        return "gate-row-missing"
    # NEVER default. An absent authentication field is missing evidence, and a
    # gate result not bound to the row's own artifact proves nothing about it.
    if gate.get("cert_hash_matches_row") != "yes":
        return "hash-unauthenticated"
    if gate.get("vc_gates") == "PASS" and gate.get("cert_invariant") == "PASS" \
       and gate.get("cert_differential") == "PASS":
        return "ACCEPTED"
    return "gate-rejected"


def main():
    if len(sys.argv) < 6:
        print(__doc__, file=sys.stderr)
        return 2
    nrows_p, ngates_p, brows_p, bgates_p, list_p = sys.argv[1:6]
    out_p = sys.argv[6] if len(sys.argv) > 6 else None

    problems = []
    _, nrows = read_tsv(nrows_p, "runtime rows", problems, ROW_FIELDS, ROW_REQUIRED)
    _, ngates = read_tsv(ngates_p, "gates", problems, len(GATE_HEADER_RUN),
                         GATE_HEADER_RUN, exact_header=GATE_HEADER_RUN)
    # The baseline carries the transition claims, so it is validated the same way.
    _, brows = read_tsv(brows_p, "baseline rows", problems, ROW_FIELDS, ROW_REQUIRED)
    _, bgates = read_tsv(bgates_p, "baseline gates", problems, len(GATE_HEADER_BASELINE),
                         GATE_HEADER_BASELINE, exact_header=GATE_HEADER_BASELINE)
    declared = [l.strip() for l in io.open(list_p, encoding="utf-8")
                if l.strip() and not l.lstrip().startswith("#")]
    # The BASELINE carries every transition claim, so its rows are held to the
    # same canonical identity as the new run's.
    check_row_identity(nrows, declared, "runtime rows", problems)
    check_row_identity(brows, declared, "baseline rows", problems)
    check_gate_identity(nrows, ngates, "gates", problems, strict_order=True)
    check_gate_identity(brows, bgates, "baseline gates", problems, strict_order=False,
                        exact_candidate_set=True)

    ng = {g["module"]: g for g in ngates}
    bg = {g["module"]: g for g in bgates}
    nby = {r["module"]: r for r in nrows}
    bby = {r["module"]: r for r in brows}

    L = []
    def say(s=""):
        L.append(s)

    # ---- schema + identity, before any number is quoted --------------------
    order = [r["module"] for r in nrows]

    say("CORE-ET 122 reconciliation")
    say("=" * 74)
    say("declared modules   %d" % len(declared))
    say("rows              %d  (unique %d)" % (len(nrows), len(set(order))))
    say("row order         %s" % ("EXACT declared order" if order == declared else "DIVERGES"))
    say("row fields        %d on every row, raw-validated before any field lookup" % ROW_FIELDS)
    if problems:
        say("reconciled        NO")
        for q in problems:
            say("  ! %s" % q)
    else:
        say("reconciled        YES")
    say("(reconciled = schema completeness, NOT coverage)")
    say()
    if problems:
        say("INFRASTRUCTURE INCOMPLETE -- no coverage figure is reported.")
        say("A count taken over rows or gates that do not reconcile is not a measurement.")
        text = "\n".join(L) + "\n"
        if out_p:
            io.open(out_p, "w", encoding="utf-8").write(text)
            print("wrote %s" % out_p)
        print(text)
        return 1

    nout = {m: outcome(nby[m], ng.get(m)) for m in order}
    bout = {m: outcome(bby[m], bg.get(m)) for m in bby}

    say("OUTCOME, this run (last stage actually reached)")
    say("-" * 74)
    counts = {}
    for m in order:
        counts[nout[m]] = counts.get(nout[m], 0) + 1
    for k in sorted(counts, key=lambda k: (-counts[k], k)):
        say("  %-22s %3d" % (k, counts[k]))
    say()
    say("  ACCEPTED COVERAGE      %d of %d" % (counts.get("ACCEPTED", 0), len(declared)))
    say("  (row says PARSED/true/RAN AND the certificate hash matches AND")
    say("   vc_gates + invariant + the real --require-verilog differential all pass)")
    say()

    say("TRANSITIONS vs the accepted baseline")
    say("-" * 74)
    moved = [(m, bout.get(m, "absent"), nout[m]) for m in order if bout.get(m, "absent") != nout[m]]
    if not moved:
        say("  none: every block reached the same stage as the accepted baseline")
    for m, b, n in moved:
        say("  %-32s %-20s -> %s" % (m, b, n))
    say()
    say("  blocks that moved      %d" % len(moved))
    gained = [m for m, b, n in moved if n == "ACCEPTED"]
    lost = [m for m, b, n in moved if b == "ACCEPTED" and n != "ACCEPTED"]
    say("  newly ACCEPTED         %d" % len(gained))
    say("  no longer ACCEPTED     %d%s" % (len(lost), ("  <-- REGRESSION" if lost else "")))
    say()

    # The ten designs the memory lowering unblocked are the point of this run.
    MEM10 = ["minion_dcache_buffer_array", "minion_dcache_metadata_array",
             "minion_dcache_replay_queue", "minion_dcache_tlb_array", "minion_tlb",
             "vpu_lane_tima", "vpu_tensora_rf", "vpu_tensorb_rf", "vpu_tensorc_rf",
             "vpu_tensortmp_rf"]
    say("THE TEN FORMERLY FRONT-END-BLOCKED MEMORY DESIGNS")
    say("-" * 74)
    for m in MEM10:
        say("  %-32s %-20s -> %s" % (m, bout.get(m, "absent"), nout.get(m, "absent")))
    say()
    say("  of the ten, ACCEPTED   %d" % sum(1 for m in MEM10 if nout.get(m) == "ACCEPTED"))
    say()
    say("TRUST BOUNDARY: corpus authentication, hashes, cgroup counters, runtime")
    say("rows and the static/differential gates are empirical translation")
    say("validation, not a generic formal proof of the C++ LGraph-to-DCERT1")
    say("transcription.")

    text = "\n".join(L) + "\n"
    if out_p:
        io.open(out_p, "w", encoding="utf-8").write(text)
        print("wrote %s" % out_p)
    print(text)
    return 1 if (problems or lost) else 0


if __name__ == "__main__":
    sys.exit(main())
