#!/usr/bin/env python3
"""Decide whether an `lhd lec` run actually proved equivalence.

This exists because grepping one field out of the result JSON does not.  The
gate it replaces ran

    lec_verdict="$(grep -oP '"verdict":"\\K[^"]*' lec_gate.json | tail -1)"
    case "$lec_verdict" in proven) ... ;;

which captured the process exit status, printed it, and never looked at it
again.  Two controlled responses walked straight through:

    process exit 7  + verdict=proven   -> runner exited 0, printed PROVEN
    status="fail"   + verdict=proven   -> runner exited 0, printed PROVEN

A `verdict` field is a claim made by a run that may itself have failed.  It is
only meaningful when the run that produced it succeeded and the rest of the
record agrees with it, so every field is checked and every disagreement is a
rejection:

  * the process must have exited 0 -- a crashed or refused run proves nothing,
    whatever it managed to write first;
  * the JSON must parse, and be an object;
  * `command` must be `lec` -- a usage rejection reports `command":"usage"`, and
    that is the exact shape that produced 162 "gate logs" nobody could use;
  * `status` must be `pass` and `exit_code` must be 0;
  * the verdict lives at `lec.verdict`, NOT at the top level -- a record with no
    `lec` object did not get as far as comparing anything -- and must be
    `proven`;
  * `exit_code` must agree with the process exit, because a record that
    disagrees with the run that wrote it is not evidence of anything.

Everything else -- missing fields, `refuted`, an undecided verdict, an input
failure -- is rejected with the reason named, so a caller can tell an
infrastructure failure from a real REFUTED.

Usage: lec_result_check.py <result.json> <process-exit-status>
Exit 0 iff the run proved equivalence.
"""

import json
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: lec_result_check.py <result.json> <process-exit-status>",
              file=sys.stderr)
        return 2
    path, raw_status = sys.argv[1], sys.argv[2]
    try:
        proc_exit = int(raw_status)
    except ValueError:
        print(f"REJECT: process exit status '{raw_status}' is not a number")
        return 1

    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
    except FileNotFoundError:
        print(f"REJECT: no result JSON at {path} -- the gate did not run")
        return 1
    except json.JSONDecodeError as e:
        print(f"REJECT: result JSON is malformed ({e}) -- not a usable record")
        return 1
    if not isinstance(doc, dict):
        print(f"REJECT: result JSON is a {type(doc).__name__}, expected an object")
        return 1

    problems = []
    if proc_exit != 0:
        problems.append(f"the lec process exited {proc_exit}; a run that failed "
                        f"proves nothing regardless of what it wrote")
    for field, want in (("command", "lec"), ("status", "pass")):
        got = doc.get(field)
        if got is None:
            problems.append(f"no `{field}` field")
        elif got != want:
            problems.append(f"`{field}` is {got!r}, expected {want!r}")
    # The verdict is nested under `lec`. Reading a top-level "verdict" instead
    # finds nothing on a real record -- and would find whatever a malformed one
    # put there.
    lec = doc.get("lec")
    if not isinstance(lec, dict):
        problems.append("no `lec` object -- the run did not get as far as "
                        "comparing anything")
        verdict = None
    else:
        verdict = lec.get("verdict")
        if verdict is None:
            problems.append("no `lec.verdict` field")
        elif verdict != "proven":
            problems.append(f"`lec.verdict` is {verdict!r}, expected 'proven'")
    rec_exit = doc.get("exit_code")
    if rec_exit is None:
        problems.append("no `exit_code` field")
    else:
        if rec_exit != 0:
            problems.append(f"recorded `exit_code` is {rec_exit}, expected 0")
        if rec_exit != proc_exit:
            problems.append(f"recorded `exit_code` {rec_exit} disagrees with the "
                            f"process exit {proc_exit}")

    if problems:
        for p in problems:
            print(f"REJECT: {p}")
        # Name the outcome class so the caller can tell these apart.
        if doc.get("command") == "usage":
            print("CLASS: usage/infrastructure -- the gate did not run")
        elif verdict == "refuted":
            print("CLASS: REFUTED -- the graphs differ")
        elif verdict in (None, "unknown"):
            print("CLASS: undecided or unreported")
        else:
            print("CLASS: inconsistent record")
        return 1

    print("ACCEPT: lec proved equivalence (command=lec status=pass "
          f"exit_code=0 lec.verdict=proven, process exit {proc_exit})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
