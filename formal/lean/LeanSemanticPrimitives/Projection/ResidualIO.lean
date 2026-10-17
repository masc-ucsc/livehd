/-
# Serializing a specialized residual

A residual lived only in the memory of the process that specialized it, so a
readiness phase in a separate process had to specialize the design AGAIN
(`gen_readiness_v2` re-ran `mixDriver`; only the memory-route driver avoided it,
by doing everything in one process).  This module writes the residual to a file
and reads it back.

## The codec, and what is proved

    Program --encProgram--> Val --postfix tokens--> List Tok --LEB128--> bytes

  * `Program ↔ Val` is `Encoding.encProgram` / `decProgram`, already proved
    (`decProgram_encProgram`), unchanged.
  * `Val ↔ List Tok`: a POSTFIX token stream, read back by a stack machine
    (`run`) that is tail-recursive -- a residual of 5M terms is read without any
    recursion as deep as the term.
  * `List Tok ↔ bytes`: one tag byte per token, unsigned LEB128 varints for the
    naturals, zig-zag for integers.

`decodeProgram_encodeProgram : decodeProgram (encodeProgram P) = some P` --
proved here from core lemmas only, about exactly the functions the drivers run.

## What is NOT proved, and how it is covered

The FILE header (certificate digest, backend, fuel, producing driver) is plain
text and is not part of the theorem.  It binds the payload to the run that made
it, so a loader can refuse a residual written for a different certificate or
fuel.  On load the driver re-runs `checkResidual` on the decoded program, so the
evaluation bound is recomputed and `checkResidual_sound` covers it.  The trust
that the residual IS `projectDesign D`'s output is the same `hproj` trust as
before -- a native specializer run -- now carried across a file whose payload
decodes provably to the bytes that run wrote.
-/

import LeanSemanticPrimitives.Projection.Encoding
import LeanSemanticPrimitives.Projection.ResidualFragment
import LeanSemanticPrimitives.Projection.AxiomGate

namespace Projection
namespace ResidualIO

/-! ## 1. `Val` as a postfix token stream -/

inductive Tok where
  | int  (z : Int)
  | bool (b : Bool)
  | nil
  | cons
  | ctor (tag arity : Nat)
deriving DecidableEq, Repr, Inhabited

mutual
/-- The postfix tokens of `v`, prepended to `acc` (an accumulator, so this is
linear, not a chain of left-nested appends). -/
def toksOnto : Val → List Tok → List Tok
  | .int z,     acc => .int z :: acc
  | .bool b,    acc => .bool b :: acc
  | .nil,       acc => .nil :: acc
  | .cons a d,  acc => toksOnto a (toksOnto d (.cons :: acc))
  | .ctor n fs, acc => toksListOnto fs (.ctor n fs.length :: acc)
def toksListOnto : List Val → List Tok → List Tok
  | [],      acc => acc
  | v :: vs, acc => toksOnto v (toksListOnto vs acc)
end

/-- Pop exactly `k` values, the top of the stack first, onto `acc`; so the
deepest popped value ends up first.  O(k) -- never measures the whole stack,
which for a long let-chain holds millions of pending siblings. -/
def popN : Nat → List Val → List Val → Option (List Val × List Val)
  | 0,     s,      acc => some (acc, s)
  | k + 1, v :: s, acc => popN k s (v :: acc)
  | _ + 1, [],     _   => none

theorem popN_append : ∀ (l s acc : List Val),
    popN l.length (l ++ s) acc = some (l.reverse ++ acc, s)
  | [],      s, acc => by simp [popN]
  | v :: l,  s, acc => by
      simp only [List.length_cons, List.cons_append, popN, popN_append l s (v :: acc)]
      simp

/-- One stack-machine step. -/
def step (s : List Val) : Tok → Option (List Val)
  | .int z    => some (.int z :: s)
  | .bool b   => some (.bool b :: s)
  | .nil      => some (.nil :: s)
  | .cons     => match s with
                 | d :: a :: s' => some (.cons a d :: s')
                 | _            => none
  | .ctor n k => match popN k s [] with
                 | some (fs, rest) => some (.ctor n fs :: rest)
                 | none            => none

/-- Run the stack machine.  Tail-recursive. -/
def run : List Tok → List Val → Option (List Val)
  | [],      s => some s
  | t :: ts, s => match step s t with
                  | some s' => run ts s'
                  | none    => none

mutual
theorem run_toksOnto : ∀ (v : Val) (r : List Tok) (s : List Val),
    run (toksOnto v r) s = run r (v :: s)
  | .int z,     r, s => by simp [toksOnto, run, step]
  | .bool b,    r, s => by simp [toksOnto, run, step]
  | .nil,       r, s => by simp [toksOnto, run, step]
  | .cons a d,  r, s => by
      rw [toksOnto, run_toksOnto a, run_toksOnto d]
      simp [run, step]
  | .ctor n fs, r, s => by
      rw [toksOnto, run_toksListOnto fs]
      have hp : popN fs.length (fs.reverse ++ s) [] = some (fs, s) := by
        have h := popN_append fs.reverse s []
        rw [List.length_reverse, List.reverse_reverse, List.append_nil] at h
        exact h
      simp only [run, step, hp]
theorem run_toksListOnto : ∀ (vs : List Val) (r : List Tok) (s : List Val),
    run (toksListOnto vs r) s = run r (vs.reverse ++ s)
  | [],      r, s => by simp [toksListOnto]
  | v :: vs, r, s => by
      rw [toksListOnto, run_toksOnto v, run_toksListOnto vs]
      simp
end

theorem run_toks (v : Val) : run (toksOnto v []) [] = some [v] := by
  rw [run_toksOnto]; rfl

/-! ## 2. Tokens as bytes -/

/-- Unsigned LEB128. -/
def varint (n : Nat) : List UInt8 :=
  if n < 128 then [UInt8.ofNat n]
  else UInt8.ofNat (n % 128 + 128) :: varint (n / 128)
termination_by n
decreasing_by omega

def readVarint : List UInt8 → Option (Nat × List UInt8)
  | []      => none
  | b :: bs =>
      if b.toNat < 128 then some (b.toNat, bs)
      else match readVarint bs with
           | some (m, r) => some (b.toNat - 128 + 128 * m, r)
           | none        => none

theorem readVarint_varint (n : Nat) (rest : List UInt8) :
    readVarint (varint n ++ rest) = some (n, rest) := by
  induction n using Nat.strongRecOn with
  | _ n ih =>
    rw [varint]
    by_cases h : n < 128
    · have h1 : (UInt8.ofNat n).toNat = n := by simp [UInt8.toNat_ofNat']; omega
      simp [h, readVarint, h1]
    · have h1 : (UInt8.ofNat (n % 128 + 128)).toNat = n % 128 + 128 := by
        simp [UInt8.toNat_ofNat']; omega
      simp only [h, if_false, List.cons_append, readVarint, h1]
      rw [if_neg (by omega), ih (n / 128) (by omega)]
      simp only [Option.some.injEq, Prod.mk.injEq, and_true]
      omega

/-- Zig-zag: non-negatives to evens, negatives to odds. -/
def zig : Int → Nat
  | .ofNat n   => 2 * n
  | .negSucc n => 2 * n + 1

def zag (m : Nat) : Int :=
  if m % 2 = 0 then .ofNat (m / 2) else .negSucc (m / 2)

theorem zag_zig : ∀ z : Int, zag (zig z) = z
  | .ofNat n => by
      have h1 : 2 * n % 2 = 0 := by omega
      have h2 : 2 * n / 2 = n := by omega
      simp [zig, zag, h1, h2]
  | .negSucc n => by
      have h1 : (2 * n + 1) % 2 = 1 := by omega
      have h2 : (2 * n + 1) / 2 = n := by omega
      simp [zig, zag, h1, h2]

def encTok : Tok → List UInt8
  | .int z      => 0 :: varint (zig z)
  | .bool false => [1]
  | .bool true  => [2]
  | .nil        => [3]
  | .cons       => [4]
  | .ctor n k   => 5 :: (varint n ++ varint k)

def decTok : List UInt8 → Option (Tok × List UInt8)
  | []      => none
  | t :: bs =>
      if t = 0 then
        match readVarint bs with
        | some (m, r) => some (.int (zag m), r)
        | none        => none
      else if t = 1 then some (.bool false, bs)
      else if t = 2 then some (.bool true, bs)
      else if t = 3 then some (.nil, bs)
      else if t = 4 then some (.cons, bs)
      else if t = 5 then
        match readVarint bs with
        | some (n, r) =>
            match readVarint r with
            | some (k, r') => some (.ctor n k, r')
            | none         => none
        | none => none
      else none

theorem decTok_encTok (t : Tok) (rest : List UInt8) :
    decTok (encTok t ++ rest) = some (t, rest) := by
  cases t with
  | int z => simp [encTok, decTok, readVarint_varint, zag_zig]
  | bool b => cases b <;> simp [encTok, decTok]
  | nil => simp [encTok, decTok]
  | cons => simp [encTok, decTok]
  | ctor n k => simp [encTok, decTok, List.append_assoc, readVarint_varint]

theorem encTok_ne_nil (t : Tok) : encTok t ≠ [] := by
  cases t with
  | bool b => cases b <;> simp [encTok]
  | _ => simp [encTok]

/-- Decode every token, with fuel (each token is at least one byte, so the
byte count is enough).  Tail-recursive. -/
def decToks : Nat → List UInt8 → List Tok → Option (List Tok)
  | _,     [],      acc => some acc.reverse
  | 0,     _ :: _,  _   => none
  | f + 1, b :: bs, acc => match decTok (b :: bs) with
                           | some (t, r) => decToks f r (t :: acc)
                           | none        => none

theorem length_flatMap_encTok (ts : List Tok) : ts.length ≤ (ts.flatMap encTok).length := by
  induction ts with
  | nil => simp
  | cons t ts ih =>
      have : 1 ≤ (encTok t).length := by
        cases h : encTok t with
        | nil => exact absurd h (encTok_ne_nil t)
        | cons _ _ => simp
      rw [List.flatMap_cons, List.length_append, List.length_cons]; omega

theorem decToks_flatMap (ts : List Tok) :
    ∀ (f : Nat) (acc : List Tok), ts.length ≤ f →
      decToks f (ts.flatMap encTok) acc = some (acc.reverse ++ ts) := by
  induction ts with
  | nil => intro f acc _; cases f <;> simp [decToks]
  | cons t ts ih =>
      intro f acc hf
      obtain ⟨f', rfl⟩ : ∃ f', f = f' + 1 := ⟨f - 1, by simp at hf; omega⟩
      rw [List.flatMap_cons]
      cases h : encTok t ++ ts.flatMap encTok with
      | nil => exact absurd (List.append_eq_nil_iff.mp h).1 (encTok_ne_nil t)
      | cons b bs =>
          simp only [decToks]
          rw [← h, decTok_encTok]
          dsimp only
          rw [ih f' (t :: acc) (by simp at hf; omega)]
          simp

/-! ## 3. The whole codec -/

def encodeVal (v : Val) : List UInt8 := (toksOnto v []).flatMap encTok

def decodeVal (bs : List UInt8) : Option Val :=
  match decToks bs.length bs [] with
  | none    => none
  | some ts =>
      match run ts [] with
      | some [v] => some v
      | _        => none

theorem decodeVal_encodeVal (v : Val) : decodeVal (encodeVal v) = some v := by
  unfold decodeVal encodeVal
  rw [decToks_flatMap _ _ [] (length_flatMap_encTok _)]
  simp [run_toks]

def encodeProgram (P : Program) : List UInt8 := encodeVal (encProgram P)

def decodeProgram (bs : List UInt8) : Option Program :=
  (decodeVal bs).bind decProgram

/-- **The codec round trip.** -/
theorem decodeProgram_encodeProgram (P : Program) :
    decodeProgram (encodeProgram P) = some P := by
  simp [decodeProgram, encodeProgram, decodeVal_encodeVal]

/-! ## 4. The file -/

/-- Text header, then the payload.  The header binds the payload to the run
that produced it; it is checked on load, not proved. -/
structure Header where
  certDigest : String   -- the certificate's fnv1a, as the drivers print it
  backend    : String   -- `hwAPVarT` (scalar) or `hwMemAP` (memory route)
  stepFuel   : Nat
  workFuel   : Nat
  producer   : String   -- the driver that specialized
deriving Repr, DecidableEq

def magic : String := "FPRES1"

/-- FNV-1a over a certificate's bytes -- the same function `dcert2_memroute`
prints, so the two report comparable digests. -/
def fnv1a (b : ByteArray) : UInt64 :=
  b.foldl (fun h c => (h ^^^ c.toUInt64) * 1099511628211) 14695981039346656037

def headerText (h : Header) : String :=
  s!"{magic}\ncert {h.certDigest}\nbackend {h.backend}\nfuel {h.stepFuel} {h.workFuel}\n\
producer {h.producer}\npayload\n"

/-- Write `P` with its header.  Returns the payload size. -/
def writeResidual (path : System.FilePath) (h : Header) (P : Program) : IO Nat := do
  let payload := encodeProgram P
  let hdr := (headerText h).toUTF8
  IO.FS.writeBinFile path (hdr ++ ⟨payload.toArray⟩)
  pure payload.length

private def parseHeader (lines : List String) : Option Header := do
  let [m, c, b, f, p, "payload"] := lines | none
  guard (m == magic)
  let some cd := c.dropPrefix? "cert "         | none
  let some bk := b.dropPrefix? "backend "      | none
  let some fu := f.dropPrefix? "fuel "         | none
  let some pr := p.dropPrefix? "producer "     | none
  let [s, w] := fu.toString.splitOn " "        | none
  let some sf := s.toNat?                      | none
  let some wf := w.toNat?                      | none
  some ⟨cd.toString, bk.toString, sf, wf, pr.toString⟩

/-- Read a residual file: the header and the decoded program.  The header ends
at the SIXTH newline; the payload is everything after it. -/
def readResidual (path : System.FilePath) : IO (Header × Program) := do
  let b ← IO.FS.readBinFile path
  let bytes := b.data.toList
  -- split off six header lines (byte 10 = '\n')
  let rec cut : Nat → List UInt8 → List UInt8 → Option (List UInt8 × List UInt8)
    | 0,     acc, rest      => some (acc.reverse, rest)
    | k + 1, acc, c :: rest => if c == 10 then cut k (c :: acc) rest else cut (k + 1) (c :: acc) rest
    | _ + 1, _,   []        => none
  let some (hb, payload) := cut 6 [] bytes
    | throw (IO.userError s!"{path}: truncated residual header")
  let some htxt := String.fromUTF8? ⟨hb.toArray⟩
    | throw (IO.userError s!"{path}: residual header is not UTF-8")
  let lines := (htxt.splitOn "\n").filter (· ≠ "")
  let some h := parseHeader lines
    | throw (IO.userError s!"{path}: malformed residual header (expected {magic})")
  let some P := decodeProgram payload
    | throw (IO.userError s!"{path}: residual payload does not decode")
  pure (h, P)

/-! ## Controls -/

#guard decodeVal (encodeVal (.ctor 7 [.int (-3), .bool true, .cons .nil (.int 300)])) ==
  some (.ctor 7 [.int (-3), .bool true, .cons .nil (.int 300)])
#guard readVarint (varint 300 ++ [9]) == some (300, [9])
#guard (List.range 600).all fun n => readVarint (varint n) == some (n, [])
#guard decodeVal [] == none
#guard decodeVal [4] == none                       -- a `cons` on an empty stack
#guard decodeVal ([3, 3] : List UInt8) == none     -- two values left: not one value

#audit_axioms popN_append
#audit_axioms run_toksOnto
#audit_axioms run_toks
#audit_axioms readVarint_varint
#audit_axioms zag_zig
#audit_axioms decTok_encTok
#audit_axioms decToks_flatMap
#audit_axioms decodeVal_encodeVal
#audit_axioms decodeProgram_encodeProgram

end ResidualIO
end Projection
