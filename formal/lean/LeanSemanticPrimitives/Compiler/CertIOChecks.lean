/-
# `CertIOChecks` — the DCERT4 codec and admission, as build-time assertions

Every `#guard` below runs while `.build-proj.sh` builds this module (it is one of the script's
roots), so a change to the codec or to admission that breaks one is a BUILD failure, the first
step of every test tier, not a test someone has to remember to run. DCERT1's codec has had such
guards since it was written (`Projection/CertLoad.lean`); this is DCERT4's.

Checked, on two certificate literals:
- `rich`, one record of every kind the format has (every source tag, operators with and without
  a payload, an output, a flop on clock 1 with an asynchronous reset and one with nothing, a
  memory with power-on words, a commit table, an async-reset image and depth 12, and clock names
  with a space, a newline and multi-byte characters): it renders to bytes that start with
  `DCERT4\n` and parses back to the same record. Not an admissible design: the codec judges
  nothing.
- `small`, an admissible design (an input, a flop, a node, a 12-word memory of a 16-word table,
  one clock): it reads back, admission (`readCertChecked`, the one every driver loads with)
  admits it, and `certOf`/`certReads` (what an emitted theorem file and a generated driver decode
  their embedded text with) give it back.
Refused, each with its own message: DCERT3, DCERT2 and DCERT1 by their magic, a file that is not
a certificate, a truncated file; and by admission, no clock domains, an output past the design, a
memory of depth 0 and one deeper than its table. `certOf` of a text that does not admit is the
empty design, and `certReads` says so.

The dynamic counterpart, on lhd's own bytes and in every driver, is
`pass/lean/tools/tests/dcert_version_check.lean` and `cert_admit_selftest.sh`.
-/
import LeanSemanticPrimitives.Compiler.CertIOCodec

namespace Compiler
namespace CertIO
namespace Checks

/-- Field by field: `DesignCert` has no `DecidableEq`, its fields do. -/
def certEq (a b : DesignCert) : Bool :=
  a.sources == b.sources && a.nodes == b.nodes && a.outputs == b.outputs &&
  a.flops == b.flops && a.memories == b.memories && a.clocks == b.clocks

/-- `parseCert (renderCert D)` is `D`. -/
def readsBack (D : DesignCert) : Bool :=
  match parseCert (renderCert D) with
  | .ok E => certEq E D
  | .error _ => false

/-- Refused, with a message that contains `msg`. -/
def refusedWith {α : Type} (msg : String) : Except String α → Bool
  | .ok _ => false
  | .error e => (e.splitOn msg).length > 1

/-- One record of every kind. -/
def rich : DesignCert where
  sources  := #[.input 0 8, .const 8 (-3), .flopQ 0 8, .flopQAsync 1 8 0 5 true, .memImg 0 4 8,
                .memConst 2 8 #[1, -2, 3, 4]]
  nodes    := #[{ op := .Op_Const (-5), width := 8, deps := #[], origin := 7 },
                { op := .Op_Sum 2, width := 9, deps := #[0, 1] },
                { op := .Op_Xor, width := 8, deps := #[0, 2], origin := 1000000000 },
                { op := .Op_MemRead, width := 8, deps := #[4, 0] },
                { op := .Op_MemWriteBE 2, width := 8, deps := #[4, 0, 1, 2] },
                { op := .Op_MemLoad 16, width := 8, deps := #[4, 5, 3] },
                { op := .Op_MemToBus 4, width := 32, deps := #[5] }]
  outputs  := #[{ slot := 0, width := 8 }, { slot := 12, width := 32 }]
  flops    := #[{ width := 8, din := 8, enable := some 1, resetPin := some 0, resetValue := -1,
                  resetActiveLow := true, clock := 1, asyncReset := true },
                { width := 8, din := 9, enable := none, resetPin := none, resetValue := 0,
                  resetActiveLow := false }]
  memories := #[{ aw := 4, dw := 8, nextImg := 10, clock := 1, init := #[1, 2, 3],
                  commits := #[{ mask := 1, nextImg := 10 }, { mask := 2, nextImg := 11 }],
                  asyncImg := some 11, depth := 12 },
                { aw := 2, dw := 1, nextImg := 4 }]
  clocks   := #[{ name := "clk" }, { name := "a b\nc" }, { name := "λ→時計" }]

/-- An admissible design: input 0 (4 bits) and input 1 (8 bits), a flop loading `in1 ^ q`, and
a 12-word memory (address width 4) that holds its image. -/
def small : DesignCert where
  sources  := #[.input 0 4, .input 1 8, .flopQ 0 8, .memImg 0 4 8]
  nodes    := #[{ op := .Op_Xor, width := 8, deps := #[1, 2] }]
  outputs  := #[{ slot := 2, width := 8 }, { slot := 4, width := 8 }]
  flops    := #[{ width := 8, din := 4, enable := none, resetPin := none, resetValue := 0,
                  resetActiveLow := false }]
  memories := #[{ aw := 4, dw := 8, nextImg := 3, depth := 12 }]
  clocks   := #[{ name := "clk" }]

def smallBytes : ByteArray := renderCert small

def withDepth (n : Nat) : DesignCert :=
  { small with memories := small.memories.map fun m => { m with depth := n } }

-- the round trip, and the magic a writer writes
#guard readsBack rich
#guard readsBack small
#guard (renderCert rich).extract 0 7 == "DCERT4\n".toUTF8
#guard smallBytes.extract 0 7 == "DCERT4\n".toUTF8

-- admission admits the admissible design; certOf and certReads give it back
#guard (match readCertChecked smallBytes with | .ok D => certEq D small | .error _ => false)
#guard certReads (String.fromUTF8! smallBytes)
#guard certEq (certOf (String.fromUTF8! smallBytes)) small

-- every earlier version is refused by its magic, by the codec and by admission
#guard refusedWith "bad magic: expected DCERT4" (parseCert (smallBytes.set! 5 51))         -- DCERT3
#guard refusedWith "bad magic: expected DCERT4" (readCertChecked (smallBytes.set! 5 51))
#guard refusedWith "bad magic: expected DCERT4" (parseCert (smallBytes.set! 5 50))         -- DCERT2
#guard refusedWith "bad magic: expected DCERT4" (readCertChecked "DCERT1\n0\n0\n0\n0\n0\n".toUTF8)
#guard refusedWith "bad magic: expected DCERT4" (readCertChecked "not a certificate\n".toUTF8)
-- a truncated file is not a shorter design
#guard refusedWith "malformed" (parseCert (smallBytes.extract 0 (smallBytes.size - 10)))

-- admission's own refusals: the codec reads each, admission refuses it
#guard (parseCert (renderCert { small with clocks := #[] })).toBool
#guard refusedWith "no clock domains" (readCertChecked (renderCert { small with clocks := #[] }))
#guard refusedWith "output 0 reads slot 5"
  (readCertChecked (renderCert { small with outputs := #[{ slot := small.numSlots, width := 8 }] }))
#guard (parseCert (renderCert (withDepth 0))).toBool
#guard refusedWith "declares depth 0" (readCertChecked (renderCert (withDepth 0)))
#guard refusedWith "declares depth 17" (readCertChecked (renderCert (withDepth 17)))
#guard (readCertChecked (renderCert (withDepth 16))).toBool

-- certOf of a text that does not admit is the empty design, and certReads says it does not admit
#guard !certReads "not a certificate\n"
#guard !certReads (String.fromUTF8! (renderCert { small with clocks := #[] }))
#guard (certOf "not a certificate\n").nodes.isEmpty && (certOf "not a certificate\n").outputs.isEmpty

end Checks
end CertIO
end Compiler
