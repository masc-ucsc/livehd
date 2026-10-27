#!/usr/bin/env python3
"""Re-verify the two front-end evidence tables against their own provenance.

WHAT THIS IS FOR. These tables are quoted as evidence, so a reader must be able
to establish -- without trusting the prose above them, the script that wrote
them, or the person who ran it -- that they describe the canonical identity set
and nothing else, that every row is well formed, that every verdict and total
follows from the rows, and that the provenance a reader would quote is real.

EVERY CHECK IS A REQUIREMENT. Two earlier revisions reported instead of
required, and review broke both:

  * the first printed counts and exited 0 on a fabricated one-row table;
  * the second guarded numeric fields with `.isdigit()`, so a non-numeric count
    SKIPPED the check; it accepted an all-zero `source_commit`, an unrelated
    `source_tree`, invented `corpus_identity` and `B_sha256` values, and a
    `B_mtime` of "not-a-time"; it accepted a refused row with no diagnostic
    code; and it accepted an `input_unusable` declaration naming a block whose
    row said something else.

So: a field that cannot be parsed is a failure, not a skipped check; identities
are RECOMPUTED from the named inputs; named commits must exist; and the
binaries' bytes are re-read and re-hashed.

Usage: verify_frontend_tables.py [ab.tsv] [classification.tsv]
"""
import collections
import datetime
import hashlib
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))

SHA256 = re.compile(r"^[0-9a-f]{64}$")
SHA16 = re.compile(r"^[0-9a-f]{16}$")
GITID = re.compile(r"^[0-9a-f]{40}$")
CODE = re.compile(r"^[a-z0-9-]+$")
INT = re.compile(r"^(0|[1-9][0-9]*)$")

AB_VERDICTS = {"IDENTICAL", "EMISSION-CHANGED", "ACCEPTANCE-CHANGED",
               "BOTH-REFUSE-SAME", "REFUSAL-CODE-CHANGED", "INPUT-UNUSABLE"}
PROBE_CHANGES = {"CLEARED", "UNCHANGED", "RENAMED", "REGRESSED", "STILL-OK"}

COMMON = ["schema", "source_commit", "source_tree", "source_clean",
          "bazel_version", "bazel_config", "compiler", "manifest",
          "manifest_sha256", "manifest_count", "corpus_root", "corpus_identity",
          "B_source_commit", "B_path", "B_sha256", "B_bytes", "B_mtime",
          "B_binding", "B_binding_sha256",
          "started", "finished", "rows", "totals"]
# Identity LISTS: empty is a real, checkable claim, but the key must be there.
MAY_BE_EMPTY = {"input_unusable", "skipped_no_diagnostic"}
COUNTS = {"manifest_count", "rows", "A_bytes", "B_bytes", "skipped_count",
          "input_unusable_count"}
HASHES = {"manifest_sha256", "corpus_identity", "baseline_identity",
          "A_sha256", "B_sha256", "A_binding_sha256", "B_binding_sha256"}
COMMITS = {"source_commit", "source_tree", "A_source_commit", "B_source_commit"}
STAMPS = {"started", "finished"}
MTIMES = {"A_mtime", "B_mtime"}


class Fail(list):
    def __call__(self, msg):
        self.append(msg)


def git(*args):
    try:
        out = subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True)
        return out.stdout.strip() if out.returncode == 0 else None
    except OSError:
        return None


def sha256_file(path):
    h = hashlib.sha256()
    with io.open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load(path, fail, label):
    """Parse the table. A REPEATED provenance key is a failure: whichever copy
    wins, a reader quoting the other one would be quoting something no tool
    ever checked."""
    prov, markers, rows, seen = {}, [], [], collections.Counter()
    if not os.path.exists(path):
        return None, None, None
    for ln in io.open(path, encoding="utf-8"):
        ln = ln.rstrip("\n")
        if ln.startswith("#"):
            body = ln[1:].strip()
            if "=" not in body:
                continue
            k, v = body.split("=", 1)
            k, v = k.strip(), v.strip()
            if k == "marker":
                markers.append(v)
            else:
                seen[k] += 1
                prov[k] = v
            continue
        if ln.strip():
            rows.append(ln.split("\t"))
    for k, n in seen.items():
        if n > 1:
            fail(f"{label}: provenance key '{k}' appears {n} times")
    return prov, markers, rows


def parse_totals(s, fail, label):
    out = {}
    for part in s.split():
        if "=" not in part:
            fail(f"{label}: totals entry {part!r} is not key=value")
            return None
        k, v = part.split("=", 1)
        if not INT.match(v):
            fail(f"{label}: totals entry {part!r} is not a canonical integer")
            return None
        out[k] = int(v)
    return out


def check_fields(prov, schema, extra, fail, label):
    for k in COMMON + extra:
        if k not in prov:
            fail(f"{label}: provenance field '{k}' is missing")
        elif prov[k] == "" and k not in MAY_BE_EMPTY:
            fail(f"{label}: provenance field '{k}' is empty")
    if prov.get("schema") != schema:
        fail(f"{label}: schema is {prov.get('schema')!r}, expected {schema!r}")
    # NOTHING is skipped because it failed to parse.
    for k in COUNTS & set(prov):
        if not INT.match(prov[k]):
            fail(f"{label}: {k}={prov[k]!r} is not a canonical integer")
    for k in HASHES & set(prov):
        if not SHA256.match(prov[k]):
            fail(f"{label}: {k} is not a 64-hex sha256: {prov[k]!r}")
    for k in COMMITS & set(prov):
        if not GITID.match(prov[k]):
            fail(f"{label}: {k} is not a 40-hex git id: {prov[k]!r}")
    for k in STAMPS & set(prov):
        try:
            datetime.datetime.strptime(prov[k], "%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            fail(f"{label}: {k}={prov[k]!r} is not an ISO-8601 UTC timestamp")
    for k in MTIMES & set(prov):
        if not re.match(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(\.\d+)? [-+]\d{4}$", prov[k]):
            fail(f"{label}: {k}={prov[k]!r} is not a parseable file timestamp")
    if prov.get("source_clean", "").split()[0:1] != ["yes"]:
        fail(f"{label}: source_clean is {prov.get('source_clean')!r}")
    if "A_sha256" in prov and prov.get("A_sha256") == prov.get("B_sha256"):
        fail(f"{label}: A and B carry the same sha256; the table compares nothing")

    # A NAMED COMMIT MUST EXIST, and the tree must be that commit's tree.
    for k in ("source_commit", "A_source_commit", "B_source_commit"):
        if k in prov and GITID.match(prov[k]):
            if git("rev-parse", "--verify", "--quiet", prov[k] + "^{commit}") is None:
                fail(f"{label}: {k}={prov[k]} is not a commit in this repository")
    sc, st = prov.get("source_commit", ""), prov.get("source_tree", "")
    if GITID.match(sc) and GITID.match(st):
        want = git("rev-parse", sc + "^{tree}")
        if want is not None and want != st:
            fail(f"{label}: source_tree {st} is not the tree of source_commit {sc} (which is {want})")


def read_binding(path):
    out = {}
    for ln in io.open(path, encoding="utf-8"):
        ln = ln.strip()
        if ln.startswith("#") or "=" not in ln:
            if ln.startswith("# schema"):
                out["schema"] = ln.split("=", 1)[1].strip()
            continue
        k, v = ln.split("=", 1)
        out[k.strip()] = v.strip()
    return out


def check_binding(prov, side, fail, label):
    """THE SOURCE-TO-BINARY BINDING.

    A retained hash says what the bytes ARE. It says nothing about where they
    came from, and review proved the difference: a 419-byte wrapper carrying the
    expected marker strings and forwarding to the other binary was published as
    if it had been built from HEAD, and the previous verifier accepted it.

    So each side must name an attestation written by `d4_build_binding.sh` --
    which builds the COMPLETE tree of an exact commit in an isolated worktree --
    and that attestation must name this side's commit, this run's configuration,
    and these exact bytes. Absent or mismatched is a failure."""
    p = prov.get(f"{side}_binding")
    want_file_sha = prov.get(f"{side}_binding_sha256")
    if not p:
        fail(f"{label}: {side} names no build binding, so its bytes are not attributable to a commit")
        return
    path = p if os.path.isabs(p) else os.path.join(ROOT, p)
    if not os.path.exists(path):
        fail(f"{label}: {side}_binding {p} is not present, so {side} is NOT bound to a commit")
        return
    got = sha256_file(path)
    if got != want_file_sha:
        fail(f"{label}: {side}_binding {p} hashes {got}, table says {want_file_sha}")
    b = read_binding(path)
    if b.get("schema") != "d4-binary-binding/1":
        fail(f"{label}: {side}_binding is not a d4-binary-binding/1 attestation")
        return
    for key, field in (("commit", f"{side}_source_commit"), ("sha256", f"{side}_sha256"),
                       ("bytes", f"{side}_bytes")):
        if b.get(key) != prov.get(field):
            fail(f"{label}: {side}_binding {key}={b.get(key)!r} does not match the table's "
                 f"{field}={prov.get(field)!r} -- these bytes are not bound to that commit")
    if b.get("config") != prov.get("bazel_config"):
        fail(f"{label}: {side}_binding was built with [{b.get('config')}] but the table records "
             f"[{prov.get('bazel_config')}]")
    tree = git("rev-parse", (b.get("commit") or "") + "^{tree}")
    if tree is not None and b.get("tree") != tree:
        fail(f"{label}: {side}_binding tree {b.get('tree')} is not the tree of its own commit")


def check_binaries(prov, fail, label):
    """The bytes, re-read and re-hashed. An absent artifact is reported as
    absent -- never as independently verified."""
    for side in ("A", "B"):
        p = prov.get(f"{side}_path")
        want = prov.get(f"{side}_sha256")
        if p is None or want is None:
            continue
        path = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if not os.path.exists(path):
            fail(f"{label}: {side}_path {p} is not present, so {side}_sha256 is NOT verified "
                 f"(rebuild it with recheck_evidence_binaries.sh)")
            continue
        got = sha256_file(path)
        if got != want:
            fail(f"{label}: {side}_path {p} hashes {got}, table says {want}")
        size = str(os.path.getsize(path))
        if prov.get(f"{side}_bytes") != size:
            fail(f"{label}: {side}_bytes={prov.get(f'{side}_bytes')} but the file is {size}")


def check_manifest(prov, fail, label):
    man = prov.get("manifest", "")
    if not man:
        return None
    path = man if os.path.isabs(man) else os.path.join(ROOT, man)
    if not os.path.exists(path):
        fail(f"{label}: manifest {man} does not exist")
        return None
    got = hashlib.sha256(io.open(path, "rb").read()).hexdigest()
    if got != prov.get("manifest_sha256"):
        fail(f"{label}: manifest {man} hashes {got}, table says {prov.get('manifest_sha256')}")
    names = [l.strip() for l in io.open(path, encoding="utf-8")
             if l.strip() and not l.strip().startswith("#")]
    if len(names) != len(set(names)):
        fail(f"{label}: manifest {man} has duplicate names")
    if prov.get("manifest_count") != str(len(names)):
        fail(f"{label}: manifest_count={prov.get('manifest_count')} but the manifest holds {len(names)}")
    return names


def corpus_identity(root, names):
    h = hashlib.sha256()
    for m in names:
        h.update(f"block {m}\n".encode())
        base = os.path.join(root, m, "lgdb_raw")
        rels = []
        for dirpath, _, files in os.walk(base):
            for f in files:
                full = os.path.join(dirpath, f)
                rels.append(os.path.relpath(full, base))
        for rel in sorted(rels):
            h.update(f"{sha256_file(os.path.join(base, rel))}  {rel}\n".encode())
    return h.hexdigest()


def baseline_identity(root, names):
    h = hashlib.sha256()
    for m in names:
        p = os.path.join(root, m, "logs", "single_edge.log")
        d = sha256_file(p) if os.path.exists(p) else ""
        h.update(f"{d}  {m}\n".encode())
    return h.hexdigest()


def check_corpus(prov, names, fail, label):
    """RECOMPUTED, not trusted. The identity covers file CONTENTS, so a fixture
    swapped for one of the same size no longer matches."""
    root = prov.get("corpus_root", "")
    if not root or names is None:
        return
    rootp = root if os.path.isabs(root) else os.path.join(ROOT, root)
    if not os.path.isdir(rootp):
        fail(f"{label}: corpus_root {root} is not a directory, so corpus_identity is NOT verified")
        return
    got = corpus_identity(rootp, names)
    if got != prov.get("corpus_identity"):
        fail(f"{label}: corpus_identity recomputes to {got}, table says {prov.get('corpus_identity')}")


def check_rows(rows, width, header, fail, label):
    if not rows:
        fail(f"{label}: no rows at all")
        return []
    if rows[0] != header:
        fail(f"{label}: header is {rows[0]}, expected {header}")
    data = rows[1:]
    if any(len(r) != width for r in data):
        n = sum(1 for r in data if len(r) != width)
        fail(f"{label}: {n} row(s) do not have exactly {width} fields")
    dup = sorted(m for m, c in collections.Counter(r[0] for r in data).items() if c > 1)
    if dup:
        fail(f"{label}: duplicate identities {dup}")
    return data


def check_unusable_dirs(prov, declared, fail, label):
    """A declared-unusable block must really have no retained input."""
    root = prov.get("corpus_root", "")
    if not root:
        return
    rootp = root if os.path.isabs(root) else os.path.join(ROOT, root)
    for m in declared:
        base = os.path.join(rootp, m, "lgdb_raw")
        n = sum(len(f) for _, _, f in os.walk(base))
        if n != 0:
            fail(f"{label}: {m} is declared INPUT-UNUSABLE but its lgdb_raw holds {n} file(s)")


def verify_ab(path, fail):
    label = "A/B"
    prov, markers, rows = load(path, fail, label)
    if prov is None:
        fail(f"{label}: {path} does not exist")
        return
    check_fields(prov, "d4-frontend-ab/1",
                 ["A_source_commit", "A_path", "A_sha256", "A_bytes", "A_mtime",
                  "A_binding", "A_binding_sha256",
                  "input_unusable_count", "input_unusable"], fail, label)
    check_binaries(prov, fail, label)
    check_binding(prov, "A", fail, label)
    check_binding(prov, "B", fail, label)
    names = check_manifest(prov, fail, label)
    check_corpus(prov, names, fail, label)
    data = check_rows(rows, 8,
                      ["module", "a_rc", "b_rc", "a_code", "b_code", "a_sha", "b_sha", "verdict"],
                      fail, label)
    if len(markers) < 5:
        fail(f"{label}: only {len(markers)} marker assertion(s) recorded; expected at least 5")
    for need, why in (("A=absent B=present", "no marker actually DISCRIMINATES A from B"),
                      ("A=present B=present", "no marker is present in both; the probe could always answer 'absent'"),
                      ("A=absent B=absent", "no marker is absent in both; the probe could always answer 'present'")):
        if not any(m.startswith(need) for m in markers):
            fail(f"{label}: {why}")

    if names is not None and sorted(r[0] for r in data) != sorted(names):
        missing = sorted(set(names) - {r[0] for r in data})
        extra = sorted({r[0] for r in data} - set(names))
        fail(f"{label}: identity set differs from the manifest (missing {missing[:5]}, extra {extra[:5]})")
    if prov.get("rows") != str(len(data)):
        fail(f"{label}: header says rows={prov.get('rows')} but there are {len(data)}")

    declared = prov.get("input_unusable", "").split()
    if prov.get("input_unusable_count") != str(len(declared)):
        fail(f"{label}: input_unusable_count={prov.get('input_unusable_count')} but "
             f"{len(declared)} identities are listed")
    check_unusable_dirs(prov, declared, fail, label)

    tally, unusable_rows = collections.Counter(), set()
    for r in data:
        if len(r) != 8:
            continue
        m, arc, brc, ac, bc, ash, bsh, v = r
        if v not in AB_VERDICTS:
            fail(f"{label}: {m}: verdict {v!r} is not an allowed verdict")
            continue
        if v == "INPUT-UNUSABLE":
            if (arc, brc, ac, bc, ash, bsh) != ("4", "4", "-", "-", "-", "-"):
                fail(f"{label}: {m}: INPUT-UNUSABLE row is not 4/4/-/-/-/-: {r}")
            unusable_rows.add(m)
            tally[v] += 1
            continue
        for rc, tag in ((arc, "a_rc"), (brc, "b_rc")):
            if rc not in ("0", "7"):
                fail(f"{label}: {m}: {tag}={rc} is outside the allowed domain {{0,7}}")
        # A REFUSED SIDE MUST NAME ITS REASON. Without this a table can compare
        # two refusals by a code neither of them carries.
        for rc, c, tag in ((arc, ac, "a"), (brc, bc, "b")):
            # `-` is NOT a code. It matches `[a-z0-9-]+` -- which is how the
            # first version of this check let a `BOTH-REFUSE-SAME` row through
            # with no reason on either side -- so it is excluded by name.
            if c == "-" or not CODE.match(c):
                fail(f"{label}: {m}: side {tag} carries no diagnostic code ({c!r})")
        # An accepted side must carry a real emission hash; a refused side must
        # not. This is what stops "both emitted nothing" reading as IDENTICAL.
        for rc, sh, tag in ((arc, ash, "a"), (brc, bsh, "b")):
            if rc == "0" and not SHA16.match(sh):
                fail(f"{label}: {m}: side {tag} accepted but carries no emission hash ({sh!r})")
            if rc != "0" and sh != "-":
                fail(f"{label}: {m}: side {tag} refused but carries an emission hash ({sh!r})")
        if arc != "0" and brc != "0":
            want = "BOTH-REFUSE-SAME" if ac == bc else "REFUSAL-CODE-CHANGED"
        elif arc != brc:
            want = "ACCEPTANCE-CHANGED"
        elif ash == bsh:
            want = "IDENTICAL"
        else:
            want = "EMISSION-CHANGED"
        if want != v:
            fail(f"{label}: {m}: verdict {v} does not follow from the row (recomputed {want})")
        tally[v] += 1

    # The declaration and the rows must be the SAME SET, both ways.
    if set(declared) != unusable_rows:
        fail(f"{label}: input_unusable declares {sorted(set(declared))} but the rows with that "
             f"verdict are {sorted(unusable_rows)}")

    printed = parse_totals(prov.get("totals", ""), fail, label)
    want = {"identical": tally["IDENTICAL"], "both-refused": tally["BOTH-REFUSE-SAME"],
            "emission-changed": tally["EMISSION-CHANGED"],
            "acceptance-changed": tally["ACCEPTANCE-CHANGED"],
            "refusal-code-changed": tally["REFUSAL-CODE-CHANGED"],
            "input-unusable": tally["INPUT-UNUSABLE"]}
    if printed != want:
        fail(f"{label}: printed totals {printed} != recomputed {want}")
    print(f"{label}: {len(data)} rows, {len(set(r[0] for r in data))} unique identities, 8 fields each")
    for k, v in sorted(want.items()):
        print(f"    {k:22s} {v}")


def verify_probe(path, fail):
    label = "classification"
    prov, markers, rows = load(path, fail, label)
    if prov is None:
        fail(f"{label}: {path} does not exist")
        return
    check_fields(prov, "d4-frontend-classification/1",
                 ["skipped_count", "skipped_no_diagnostic", "input_unusable_count",
                  "input_unusable", "baseline_identity"], fail, label)
    check_binaries(prov, fail, label)
    check_binding(prov, "B", fail, label)
    names = check_manifest(prov, fail, label)
    check_corpus(prov, names, fail, label)
    data = check_rows(rows, 6,
                      ["module", "baseline_refused", "baseline_code", "now_rc", "now_code", "change"],
                      fail, label)
    skipped = prov.get("skipped_no_diagnostic", "").split()
    unusable = prov.get("input_unusable", "").split()
    for k, lst in (("skipped_count", skipped), ("input_unusable_count", unusable)):
        if prov.get(k) != str(len(lst)):
            fail(f"{label}: {k}={prov.get(k)} but {len(lst)} identities are listed")
    check_unusable_dirs(prov, unusable, fail, label)
    if prov.get("rows") != str(len(data)):
        fail(f"{label}: header says rows={prov.get('rows')} but there are {len(data)}")

    ids = [r[0] for r in data]
    if names is not None:
        covered = sorted(ids + skipped + unusable)
        if covered != sorted(names):
            missing = sorted(set(names) - set(covered))
            extra = sorted(set(covered) - set(names))
            fail(f"{label}: rows+skipped+unusable do not cover the manifest "
                 f"(missing {missing[:5]}, extra {extra[:5]})")
        if len(set(covered)) != len(covered):
            fail(f"{label}: an identity appears in more than one of rows/skipped/unusable")

    # THE HISTORICAL COLUMNS COME FROM THE RETAINED BASELINE LOGS, which live
    # outside `lgdb_raw` and so outside the corpus identity. Two things are
    # therefore done with them, because one is not enough: their bytes are
    # DIGESTED (so the logs cannot be edited under a table that still matches),
    # and each row's two historical fields are RE-DERIVED from the log it
    # claims to quote (so the ROW cannot be edited either). Review turned
    # `debug_breakpoint` from a historical non-refusal into an invented refusal,
    # adjusted the self-reported totals, and the previous verifier accepted it:
    # a table that only has to agree with itself will.
    root = prov.get("corpus_root", "")
    rootp = (root if os.path.isabs(root) else os.path.join(ROOT, root)) if root else None
    if rootp and ids:
        got = baseline_identity(rootp, ids)
        if got != prov.get("baseline_identity"):
            fail(f"{label}: baseline_identity recomputes to {got}, table says {prov.get('baseline_identity')}")
        for r in data:
            if len(r) != 6:
                continue
            m, was, base = r[0], r[1], r[2]
            log = os.path.join(rootp, m, "logs", "single_edge.log")
            if not os.path.exists(log):
                fail(f"{label}: {m}: the baseline log it quotes does not exist")
                continue
            txt = io.open(log, encoding="utf-8", errors="replace").read()
            codes = re.findall(r'"code":"([a-z0-9-]+)"', txt)
            want_code = codes[0] if codes else ""
            if want_code == "normalize-refused":
                want_code = codes[1] if len(codes) > 1 else ""
            want_was = "yes" if '"severity":"error"' in txt else "no"
            if base != want_code:
                fail(f"{label}: {m}: baseline_code={base!r} but its log says {want_code!r}")
            if was != want_was:
                fail(f"{label}: {m}: baseline_refused={was!r} but its log says {want_was!r}")

    tally, nref = collections.Counter(), 0
    for r in data:
        if len(r) != 6:
            continue
        m, was, base, rc, now, ch = r
        if was not in ("yes", "no"):
            fail(f"{label}: {m}: baseline_refused={was!r}")
        if rc not in ("0", "7"):
            fail(f"{label}: {m}: now_rc={rc} is outside the allowed domain {{0,7}}")
        if base == "-" or not CODE.match(base):
            fail(f"{label}: {m}: baseline_code={base!r} is not a diagnostic code")
        if rc != "0" and (now == "-" or not CODE.match(now)):
            fail(f"{label}: {m}: refused with no diagnostic code ({now!r})")
        if ch not in PROBE_CHANGES:
            fail(f"{label}: {m}: change={ch!r} is not an allowed status")
            continue
        if was == "yes":
            nref += 1
            want = "CLEARED" if rc == "0" else ("UNCHANGED" if now == base else "RENAMED")
        else:
            want = "STILL-OK" if rc == "0" else "REGRESSED"
        if want != ch:
            fail(f"{label}: {m}: change={ch} does not follow from the row (recomputed {want})")
        tally[ch] += 1

    printed = parse_totals(prov.get("totals", ""), fail, label)
    want = {"baseline_refusals": nref, "cleared": tally["CLEARED"],
            "unchanged": tally["UNCHANGED"], "renamed": tally["RENAMED"],
            "regressed": tally["REGRESSED"], "still-ok": tally["STILL-OK"]}
    if printed != want:
        fail(f"{label}: printed totals {printed} != recomputed {want}")
    print(f"{label}: {len(data)} rows + {len(skipped)} skipped + {len(unusable)} unusable = "
          f"{len(data) + len(skipped) + len(unusable)} manifest identities, 6 fields each")
    for k, v in sorted(want.items()):
        print(f"    {k:22s} {v}")


def rebuild_bindings(paths, fail):
    """THE REBUILD, and it is not optional.

    An attestation is an unsigned editable text file. Checking that its claims
    agree with the table's claims establishes only that two self-asserted
    objects agree -- review hand-wrote a binding naming the right commit, tree,
    configuration, hash and size for a 166-byte forwarding wrapper, and both the
    producer and this verifier accepted it. The only thing that settles where
    bytes came from is building that commit's complete tree again and comparing,
    so that is what happens here."""
    script = os.path.join(HERE, "d4_build_binding.sh")
    for p in sorted(set(paths)):
        path = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if not os.path.exists(path):
            fail(f"rebuild: binding {p} is not present, so its claims cannot be re-derived")
            continue
        print(f"\nrebuilding the exact tree named by {os.path.basename(path)} ...")
        r = subprocess.run([script, "check", path], cwd=ROOT, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip().splitlines()
        for ln in out[-4:]:
            print("    " + ln)
        if r.returncode != 0:
            fail(f"rebuild: {p} does not reproduce its own claims (exit {r.returncode})")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    unknown = flags - {"--skip-rebuild"}
    if unknown:
        print(f"unknown option(s): {' '.join(sorted(unknown))}")
        return 1
    ab = args[0] if len(args) > 0 else os.path.join(HERE, "coreet_gate_latch_failclosed_ab.tsv")
    pr = args[1] if len(args) > 1 else os.path.join(HERE, "coreet_frontend_refusals.tsv")
    fail = Fail()
    verify_ab(ab, fail)
    verify_probe(pr, fail)

    bindings = []
    for path, keys in ((ab, ("A_binding", "B_binding")), (pr, ("B_binding",))):
        prov, _, _ = load(path, Fail(), "scan")
        if prov:
            bindings += [prov[k] for k in keys if prov.get(k)]

    skipped = "--skip-rebuild" in flags
    if not skipped and not fail:
        rebuild_bindings(bindings, fail)

    if fail:
        print()
        for f in fail:
            print("FAIL: " + f)
        print(f"\n{len(fail)} failure(s)")
        return 1
    if skipped:
        # A DISTINCT exit code. `--skip-rebuild` checks the tables and nothing
        # about the binaries, so it must never look like a clean verification:
        # negatives in the control suite require exactly 1, so a skip cannot
        # stand in for a rejection either.
        print("\nTABLES VERIFY -- but the BINARIES ARE NOT AUTHENTICATED (rebuild skipped)")
        return 2
    print("\nBOTH TABLES VERIFY, and both binaries rebuild from their named commits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
