/-
`Kit.LeanText` -- Lean source text for the theorem files the proof tools emit (`sym_verify`,
`sym_refmap`, `ckpt_reach`): ONE writer of string literals, so a text embedded in any tool's
theorem file reads back as the same text. Untrusted: a theorem file is checked when Lean compiles
it. No theorems.
-/
namespace HDLProof.Kit

/-- `s` as a Lean string literal: `"`, `\` and newline escaped, every other character as it is.
`pass/lean/xpatch/checks.py` (`lean_str_at`) reads exactly these three escapes back. -/
def leanStr (s : String) : String :=
  "\"" ++ (s.toList.map fun c =>
    if c == '"' then "\\\"" else if c == '\\' then "\\\\" else if c == '\n' then "\\n" else c.toString).foldl (· ++ ·) "" ++ "\""

end HDLProof.Kit
