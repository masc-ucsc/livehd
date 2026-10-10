#!/usr/bin/env python3
"""Where an emitted `<Top>_Lgraph.lean` stops being a certificate.

Two tools need the same boundary. `make_sim_launcher.py` keeps the certificate
and drops the residual half so a launcher elaborates without the verified
compiler or Mathlib; `direct_sweep.py` does the same and appends a probe. Both
also redirect the `CompileDesign` import to `DirectTrace`, which is what makes
the cut load-bearing: once that import is gone, anything kept from the residual
half cannot elaborate.

This lives in one place because both copies had the same two defects, and a
repair applied to one copy is a repair that drifts.

  * `cut = len(text)` on failure -- keep EVERYTHING, having already removed the
    import the kept code needs -- reported as a success.
  * cutting AT the `_step` definition, which in every emitted certificate
    carries a two-line doc comment, so the module ends `/-- ... -/` with
    nothing after it. Lean rejects a doc comment with no declaration.

A boundary that cannot be located is an ERROR here, never a guess.
"""

import re

MARKER = "/-- Compile-and-run."
STEP_RE = re.compile(r"^def\s+\w+_step\s*:", re.M)


def back_over_attached_comment(text, cut):
    """Move a cut back over a doc comment attached to the declaration at `cut`.

    Returns None when the comment's opening cannot be located unambiguously --
    a nested or unmatched block -- which the caller turns into a refusal.
    """
    j = len(text[:cut].rstrip())
    if not text[:j].endswith("-/"):
        return cut
    k = text.rfind("/-", 0, j)
    if k == -1 or text.find("-/", k, j - 2) != -1:
        return None
    return k


def residual_cut(text):
    """-> (index, "") to cut at `index`, or (None, reason) to refuse.

    The marker is preferred because the exporter writes it; the `_step`
    definition is the fallback for an older or newer wording.
    """
    cut = text.find(MARKER)
    if cut >= 0:
        return cut, ""
    m = STEP_RE.search(text)
    if m is None:
        return None, (f"cannot locate the residual half: no `{MARKER}` marker and no "
                      f"`_step` definition")
    cut = back_over_attached_comment(text, m.start())
    if cut is None:
        return None, ("the `_step` definition carries a comment whose opening cannot be "
                      "located unambiguously")
    return cut, ""


def check_kept(text, cert_re):
    """-> "" if the kept text is a usable certificate module, else the reason."""
    if not cert_re.search(text):
        return "the cut removed the `_designCert` definition itself"
    if text.rstrip().endswith("-/") or text.count("/-") != text.count("-/"):
        return "the cut leaves an unbalanced or dangling comment"
    return ""
