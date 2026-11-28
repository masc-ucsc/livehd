/-
`Kit.LeanText` -- Lean source text for the theorem files the proof tools emit (`sym_verify`,
`sym_refmap`, `ckpt_reach`): ONE writer of string literals, so a text embedded in any tool's
theorem file reads back as the same text, and ONE writer of byte strings (the LRAT proofs). Untrusted: a theorem file is checked when Lean compiles
it. No theorems.
-/
namespace HDLProof.Kit

/-- `s` as a Lean string literal: `"`, `\` and newline escaped, every other character as it is.
`pass/lean/xpatch/checks.py` (`lean_str_at`) reads exactly these three escapes back. -/
def leanStr (s : String) : String :=
  "\"" ++ (s.toList.map fun c =>
    if c == '"' then "\\\"" else if c == '\\' then "\\\\" else if c == '\n' then "\\n" else c.toString).foldl (· ++ ·) "" ++ "\""

def hexDigit (n : Nat) : Char := "0123456789abcdef".toList.getD n '0'

/-- A byte string (an LRAT proof) as a hex string the file decodes (`hexBytes`, whose definition
`hexBytesDef` writes into the file): an array literal of its bytes exceeds the elaborator's
recursion depth for all but the smallest proofs (a 16 KB proof already does). -/
def leanBytes (b : ByteArray) : String :=
  "hexBytes \"" ++ String.ofList (b.toList.flatMap fun x => [hexDigit (x.toNat / 16), hexDigit (x.toNat % 16)]) ++ "\""

def hexBytesDef : String := "/-- A byte string from its hex digits. -/
def hexBytes (s : String) : ByteArray := Id.run do
  let cs := s.toList.toArray
  let hv (c : Char) : UInt8 := if c.isDigit then (c.toNat - 48).toUInt8 else (c.toNat - 87).toUInt8
  let mut out := ByteArray.empty
  for i in [0:cs.size / 2] do
    out := out.push (hv cs[2 * i]! * 16 + hv cs[2 * i + 1]!)
  return out

"

end HDLProof.Kit
