#!/usr/bin/env python3
"""Find C/C++ functions that are unused, or used only from test code.

Pipeline:
  1. universal-ctags enumerates function definitions (with body extents) and
     prototypes.
  2. All sources are scanned once: comments and string literals are stripped,
     then every identifier occurrence is indexed (name -> file:line).
  3. For each function, references are counted after discarding its own
     definition body, its prototypes, and (optionally) test code.

Classification:
  UNUSED    - no references anywhere outside the function's own definition(s)
              and prototype lines.
  TEST-ONLY - every reference is in a test file (basename contains "test" or
              any path component is "test"/"tests").

Caveats (all conservative -- they may hide a deletable function, never flag a
used one):
  - References are matched by NAME only: overloads and same-named methods on
    different classes are conflated.
  - A mention in a macro body, template, or #if-0 block counts as a use.
  - Functions found via ADL/virtual dispatch are matched by name, so they
    still register as used.

Typical use:
  scripts/find_unused_funcs.py                        # whole repo
  scripts/find_unused_funcs.py --defs-under core      # only report core/ defs
  scripts/find_unused_funcs.py --ref-dir ../some_dependent_repo
"""

import argparse
import collections
import json
import os
import re
import subprocess
import sys

SRC_EXT = {".cpp", ".cc", ".cxx", ".c", ".hpp", ".hh", ".hxx", ".h", ".ipp"}
DEFAULT_EXCLUDES = [".git", "cov", "node_modules", ".claude", "external", "third_party"]

IDENT_RE = re.compile(r"\b[A-Za-z_]\w*\b")
RAW_STR_RE = re.compile(r'(?:u8|[uUL])?R"([^()\\ \t\v\f\n]{0,16})\(')
PASTE_RE = re.compile(r"(\w+)\s*##|##\s*(\w+)")  # write_##NAME / NAME##_op

SKIP_NAMES = {"main", "wmain", "LLVMFuzzerTestOneInput",
              "TEST", "TEST_F", "TEST_P", "EXPECT_EQ", "ASSERT_EQ"}


def is_test_path(path):
    parts = path.split(os.sep)
    if any(p in ("test", "tests") for p in parts):
        return True
    return "test" in os.path.basename(path).lower()


def is_excluded(name, excludes):
    return name in excludes or name.startswith("bazel-")


def strip_comments_and_strings(text):
    """Blank out comments, string and char literals, preserving newlines."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and nxt == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif c == '"':
            # raw string: only if an R"… opener's quote lands exactly at i
            raw = None
            for back in (3, 2, 1, 0):
                m = RAW_STR_RE.match(text, i - back) if i >= back else None
                if m and m.start() + m.group(0).index('"') == i:
                    raw = m
                    break
            if raw:
                end = text.find(')' + raw.group(1) + '"', raw.end())
                end = (end + len(raw.group(1)) + 2) if end != -1 else n
                for j in range(i, min(end, n)):
                    if text[j] != "\n":
                        out[j] = " "
                i = end
            else:
                out[i] = " "
                i += 1
                while i < n and text[i] != '"':
                    if text[i] == "\\" and i + 1 < n:
                        out[i] = " "
                        i += 1
                    if text[i] != "\n":
                        out[i] = " "
                    i += 1
                if i < n:
                    out[i] = " "
                    i += 1
        elif c == "'":
            prev = text[i - 1] if i > 0 else ""
            if prev.isalnum() or prev == "_":  # digit separator 1'000'000
                i += 1
                continue
            out[i] = " "
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\" and i + 1 < n:
                    out[i] = " "
                    i += 1
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def collect_source_files(roots, excludes):
    files = []
    for root in roots:
        if os.path.isfile(root):
            files.append(os.path.normpath(root))
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not is_excluded(d, excludes)]
            for f in filenames:
                if os.path.splitext(f)[1] in SRC_EXT:
                    files.append(os.path.normpath(os.path.join(dirpath, f)))
    return files


def run_ctags(roots, excludes):
    cmd = ["ctags", "--languages=C,C++", "--kinds-c++=fp", "--kinds-c=fp",
           "--fields=+ne", "--extras=-F", "--output-format=json",
           "-f", "-", "-R"]
    for e in excludes:
        cmd += ["--exclude=" + e, "--exclude=bazel-*"]
    cmd += roots
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    defs, protos = [], collections.defaultdict(set)
    for line in res.stdout.splitlines():
        try:
            tag = json.loads(line)
        except json.JSONDecodeError:
            continue
        if tag.get("_type") != "tag":
            continue
        name = tag["name"]
        if (name in SKIP_NAMES or name.startswith("operator")
                or name.startswith("~") or name.startswith("__")
                or name.startswith("process_")):
            # process_* methods are dispatched through macro-generated virtual
            # hooks, so they have no direct callers and always look unused
            continue
        if tag.get("kind") == "function":
            defs.append({"name": name, "path": os.path.normpath(tag["path"]),
                         "line": tag["line"], "end": tag.get("end", tag["line"]),
                         "scope": tag.get("scope", "")})
        elif tag.get("kind") == "prototype":
            protos[name].add((os.path.normpath(tag["path"]), tag["line"]))
    return defs, protos


def index_references(files):
    refs = collections.defaultdict(list)  # name -> [(path, line)]
    paste_pre, paste_suf = set(), set()   # ##-token-paste fragments seen in macros
    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        clean = strip_comments_and_strings(text)
        for m in PASTE_RE.finditer(clean):
            if m.group(1) and len(m.group(1)) >= 2:
                paste_pre.add(m.group(1))
            if m.group(2) and len(m.group(2)) >= 2:
                paste_suf.add(m.group(2))
        for lineno, line in enumerate(clean.split("\n"), 1):
            if line.lstrip().startswith("#include"):
                continue
            for m in IDENT_RE.finditer(line):
                refs[m.group(0)].append((path, lineno))
    return refs, paste_pre, paste_suf


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("roots", nargs="*", default=["."],
                    help="trees to scan for definitions AND references (default .)")
    ap.add_argument("--defs-under", action="append", default=[],
                    help="only report functions defined under this path (repeatable)")
    ap.add_argument("--ref-dir", action="append", default=[],
                    help="extra tree searched for references only, e.g. a dependent repo (repeatable)")
    ap.add_argument("--exclude", action="append", default=[],
                    help="extra directory name to skip (repeatable)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()
    roots = args.roots or ["."]
    excludes = set(DEFAULT_EXCLUDES) | set(args.exclude)

    defs, protos = run_ctags(roots, excludes)
    files = collect_source_files(roots + args.ref_dir, excludes)
    refs, paste_pre, paste_suf = index_references(files)

    def macro_pasted(name):
        return (any(name.startswith(p) and len(name) > len(p) for p in paste_pre)
                or any(name.endswith(s) and len(name) > len(s) for s in paste_suf))

    # body extents per name, so a self-call or an overload's body line is
    # not counted as an outside reference
    bodies = collections.defaultdict(list)
    for d in defs:
        bodies[d["name"]].append((d["path"], d["line"], d["end"]))

    def outside_refs(name):
        keep = []
        for path, line in refs.get(name, []):
            if any(path == p and ln <= line <= end for p, ln, end in bodies[name]):
                continue
            if (path, line) in protos.get(name, ()):
                continue
            keep.append((path, line))
        return keep

    defs_under = [os.path.normpath(p) for p in args.defs_under]
    seen = set()
    unused, test_only, pasted = [], [], []
    for d in sorted(defs, key=lambda d: (d["path"], d["line"])):
        key = (d["name"], d["path"], d["line"])
        if key in seen:
            continue
        seen.add(key)
        if is_test_path(d["path"]):
            continue  # test helpers are out of scope
        if defs_under and not any(
                os.path.normpath(d["path"]).startswith(p + os.sep)
                or os.path.normpath(os.path.dirname(d["path"])) == p
                for p in defs_under):
            continue
        out = outside_refs(d["name"])
        if out and not all(is_test_path(p) for p, _ in out):
            continue  # has non-test callers
        if macro_pasted(d["name"]):
            pasted.append(d)  # may be called via a ##-pasted name; can't tell
        elif not out:
            unused.append(d)
        else:
            test_only.append((d, out))

    if args.json:
        print(json.dumps({
            "unused": unused,
            "test_only": [{"def": d, "callers": [f"{p}:{l}" for p, l in out]}
                          for d, out in test_only],
            "macro_paste_uncertain": pasted}, indent=2))
        return

    print(f"== UNUSED ({len(unused)}) — no references outside their own definition ==")
    for d in unused:
        scope = f"  [{d['scope']}]" if d["scope"] else ""
        print(f"  {d['path']}:{d['line']}  {d['name']}{scope}")

    print(f"\n== TEST-ONLY ({len(test_only)}) — referenced only from test code ==")
    for d, out in test_only:
        scope = f"  [{d['scope']}]" if d["scope"] else ""
        print(f"  {d['path']}:{d['line']}  {d['name']}{scope}")
        for p, l in sorted(set(out))[:8]:
            print(f"      <- {p}:{l}")
        if len(set(out)) > 8:
            print(f"      ... {len(set(out)) - 8} more")

    if pasted:
        print(f"\n== UNCERTAIN ({len(pasted)}) — no direct callers, but the name matches "
              "a ##-token-paste pattern (may be macro-dispatched) ==")
        for d in pasted:
            scope = f"  [{d['scope']}]" if d["scope"] else ""
            print(f"  {d['path']}:{d['line']}  {d['name']}{scope}")

    if unused or test_only:
        print("\nNote: name-based matching — verify overloads/same-named methods "
              "before deleting. Sibling repos can be checked with --ref-dir.")


if __name__ == "__main__":
    main()
