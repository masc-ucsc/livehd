/-
The DCERT1 round trip: `parseCert (renderCert D) = .ok D`, for EVERY `DesignCert`.

WHAT THIS DOES AND DOES NOT ESTABLISH.  It verifies the Lean renderer/parser
pair: bytes produced by `renderCert` are read back as the same certificate, with
no restriction on widths, array lengths, Int magnitudes or which constructors
appear.  It says NOTHING about the C++ exporter.  `pass_lean.cpp` emits DCERT1
directly, and that its output equals `renderCert D` for the design it walked is a
TRANSLATION-VALIDATION link, currently supported by the differential parity test
`//pass/lean:dcert_parity` and by the byte-exact round trip on real
certificates -- not by this theorem.  The runtime input path therefore still
rests on that link; what this removes is the possibility of the Lean reader and
the Lean writer disagreeing with each other, which is what `parseCert` being
`partial` and unverified previously left open.

PROOF SHAPE.  The only thing that decides what `f (ofList l) i` returns is
`l.drop i`.  So every lemma takes `l.drop i = <rendered bytes> ++ rest` and
concludes that the reader returns the rendered value and lands at
`i + length`.  Composition is then `List.drop_append` arithmetic rather than
reasoning about ByteArray slices.

The format cooperates: `renderCert` writes a separator before every number after
the first in a record and a newline after every record, so whatever follows a
number begins with whitespace or is the end of the buffer.  That is exactly the
side condition `readNat` needs in order to stop.
-/
import LeanSemanticPrimitives.Compiler.CertIO

namespace Compiler
namespace CertIO

set_option linter.unusedSimpArgs false
set_option maxRecDepth 8000

--------------------------------------------------------------------------------
-- 0.  ByteArray-from-list bridge
--------------------------------------------------------------------------------

/-- The buffer the renderer hands the parser. -/
abbrev ofList (l : List UInt8) : ByteArray := ⟨l.toArray⟩

@[simp] theorem ofList_size (l : List UInt8) : (ofList l).size = l.length := by
  simp [ByteArray.size]

theorem ofList_get (l : List UInt8) (i : Nat) (h : i < l.length) :
    (ofList l)[i]'(by simpa [ByteArray.size] using h) = l[i] := rfl

/-- What the cursor can still see.  Every reader below is a function of this. -/
abbrev rem (l : List UInt8) (i : Nat) : List UInt8 := l.drop i

theorem rem_lt (l : List UInt8) (i : Nat) (h : i < l.length) :
    rem l i = l[i] :: rem l (i + 1) := by
  exact List.drop_eq_getElem_cons h

theorem rem_ge (l : List UInt8) (i : Nat) (h : l.length ≤ i) : rem l i = [] :=
  List.drop_eq_nil_of_le h

--------------------------------------------------------------------------------
-- 1.  skipWs
--------------------------------------------------------------------------------

/-- Whitespace is skipped, and nothing else is. -/
theorem skipWs_eq (l : List UInt8) (i : Nat) :
    skipWs (ofList l) i
      = if h : i < l.length then (if isWs l[i] then skipWs (ofList l) (i + 1) else i) else i := by
  rw [skipWs]
  split
  · next h =>
    rw [dif_pos (by simpa [ByteArray.size] using h)]
    simp [ofList_get l i (by simpa [ByteArray.size] using h)]
  · next h =>
    rw [dif_neg (by simpa [ByteArray.size] using h)]

/-- `ws` is entirely whitespace and what follows it is not, so the cursor lands
exactly past `ws`. -/
theorem skipWs_append (l : List UInt8) (i : Nat) (ws rest : List UInt8)
    (hd : rem l i = ws ++ rest)
    (hws : ∀ c ∈ ws, isWs c)
    (hstop : ∀ c, rest.head? = some c → ¬ isWs c) :
    skipWs (ofList l) i = i + ws.length := by
  induction ws generalizing i with
  | nil =>
      simp only [List.nil_append] at hd
      rw [skipWs_eq]
      split
      · next h =>
        have : l[i] :: rem l (i + 1) = rest := by rw [← rem_lt l i h]; exact hd
        have hhead : rest.head? = some l[i] := by rw [← this]; simp
        simp [hstop _ hhead]
      · next h => simp
  | cons c cs ih =>
      have hi : i < l.length := by
        by_contra hge
        rw [rem_ge l i (Nat.le_of_not_lt hge)] at hd
        exact (List.cons_ne_nil c (cs ++ rest)) hd.symm
      have hc : l[i] = c := by
        have := rem_lt l i hi
        rw [this] at hd
        exact (List.cons.inj hd).1
      have htl : rem l (i + 1) = cs ++ rest := by
        have := rem_lt l i hi
        rw [this] at hd
        exact (List.cons.inj hd).2
      rw [skipWs_eq, dif_pos hi, if_pos (by rw [hc]; exact hws c (by simp))]
      rw [ih (i + 1) htl (fun x hx => hws x (by simp [hx]))]
      simp [List.length_cons]
      omega

--------------------------------------------------------------------------------
-- 2.  Numbers
--------------------------------------------------------------------------------

/-- Big-endian value of a digit list -- what `readNatAux` accumulates. -/
def dval : List UInt8 → Nat
  | []      => 0
  | c :: cs => (c.toNat - 48) * 10 ^ cs.length + dval cs

theorem readNatAux_eq (l : List UInt8) (i acc : Nat) :
    readNatAux (ofList l) i acc
      = if h : i < l.length then
          (if isDigit l[i] then readNatAux (ofList l) (i + 1) (acc * 10 + (l[i].toNat - 48))
           else (acc, i))
        else (acc, i) := by
  rw [readNatAux]
  split
  . next h =>
    rw [dif_pos (by simpa [ByteArray.size] using h)]
    simp [ofList_get l i (by simpa [ByteArray.size] using h)]
  . next h =>
    rw [dif_neg (by simpa [ByteArray.size] using h)]

/-- The scanner consumes exactly the digits in front of it. -/
theorem readNatAux_digits (l : List UInt8) (ds rest : List UInt8)
    (hdig : ∀ c ∈ ds, isDigit c)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    ∀ i acc, rem l i = ds ++ rest →
      readNatAux (ofList l) i acc = (acc * 10 ^ ds.length + dval ds, i + ds.length) := by
  induction ds with
  | nil =>
      intro i acc hd
      simp only [List.nil_append] at hd
      rw [readNatAux_eq]
      split
      · next h =>
        have hcons : l[i] :: rem l (i + 1) = rest := by rw [← rem_lt l i h]; exact hd
        have hhead : rest.head? = some l[i] := by rw [← hcons]; simp
        simp [hstop _ hhead, dval]
      · next h => simp [dval]
  | cons c cs ih =>
      intro i acc hd
      have hi : i < l.length := by
        by_contra hge
        rw [rem_ge l i (Nat.le_of_not_lt hge)] at hd
        exact (List.cons_ne_nil c (cs ++ rest)) hd.symm
      have hc : l[i] = c := by
        have hr := rem_lt l i hi; rw [hr] at hd; exact (List.cons.inj hd).1
      have htl : rem l (i + 1) = cs ++ rest := by
        have hr := rem_lt l i hi; rw [hr] at hd; exact (List.cons.inj hd).2
      rw [readNatAux_eq, dif_pos hi, if_pos (by rw [hc]; exact hdig c (by simp))]
      rw [ih (fun x hx => hdig x (by simp [hx])) (i + 1) _ htl, hc]
      simp [dval, List.length_cons, Nat.pow_succ]
      constructor
      · ring
      · omega

theorem dval_append_singleton (xs : List UInt8) (c : UInt8) :
    dval (xs ++ [c]) = dval xs * 10 + (c.toNat - 48) := by
  induction xs with
  | nil => simp [dval]
  | cons x xs ih =>
      simp only [List.cons_append, dval, ih, List.length_append, List.length_cons,
                 List.length_nil]
      ring

/-- A digit byte's `toNat` is the code point: `48 + d` never wraps. -/
theorem toNat_digit (d : Nat) (h : d < 10) : (UInt8.ofNat (48 + d)).toNat = 48 + d :=
  UInt8.toNat_ofNat_of_lt' (by simp [UInt8.size]; omega)

theorem isDigit_ofNat (d : Nat) (h : d < 10) : isDigit (UInt8.ofNat (48 + d)) := by
  simp only [isDigit, Bool.and_eq_true, decide_eq_true_eq, UInt8.le_iff_toNat_le,
             toNat_digit d h, show (48 : UInt8).toNat = 48 from rfl,
             show (57 : UInt8).toNat = 57 from rfl]
  omega

theorem natBytes_digits (n : Nat) : ∀ c ∈ natBytes n, isDigit c := by
  induction n using Nat.strong_induction_on with
  | _ n ih =>
      rw [natBytes]
      split
      · next h =>
        intro c hc
        simp only [List.mem_singleton] at hc
        subst hc
        exact isDigit_ofNat n h
      · next h =>
        intro c hc
        rcases List.mem_append.mp hc with hc' | hc'
        · exact ih (n / 10) (Nat.div_lt_self (by omega) (by omega)) c hc'
        · simp only [List.mem_singleton] at hc'
          subst hc'
          exact isDigit_ofNat (n % 10) (Nat.mod_lt _ (by omega))

theorem dval_natBytes (n : Nat) : dval (natBytes n) = n := by
  induction n using Nat.strong_induction_on with
  | _ n ih =>
      rw [natBytes]
      split
      · next h =>
        have hone : dval [UInt8.ofNat (48 + n)] = (UInt8.ofNat (48 + n)).toNat - 48 := by
          simp [dval]
        rw [hone, toNat_digit n h]
        omega
      · next h =>
        rw [dval_append_singleton, ih (n / 10) (Nat.div_lt_self (by omega) (by omega)),
            toNat_digit (n % 10) (Nat.mod_lt _ (by omega))]
        omega

theorem natBytes_ne_nil (n : Nat) : natBytes n ≠ [] := by
  rw [natBytes]
  split
  · simp
  · simp

/-- A rendered number never starts with whitespace, so `readNat`'s leading
`skipWs` is a no-op on it. -/
theorem natBytes_head_not_ws (n : Nat) (c : UInt8) (h : (natBytes n).head? = some c) :
    ¬ isWs c := by
  have hd : isDigit c := natBytes_digits n c (List.mem_of_mem_head? h)
  simp only [isDigit, Bool.and_eq_true, decide_eq_true_eq, UInt8.le_iff_toNat_le] at hd
  have h48 : (48 : UInt8).toNat ≤ c.toNat := hd.1
  have h57 : c.toNat ≤ (57 : UInt8).toNat := hd.2
  simp only [show (48 : UInt8).toNat = 48 from rfl, show (57 : UInt8).toNat = 57 from rfl]
    at h48 h57
  intro hw
  have hval : c.toNat = 32 ∨ c.toNat = 10 ∨ c.toNat = 13 ∨ c.toNat = 9 := by
    simp only [isWs, Bool.or_eq_true, beq_iff_eq] at hw
    rcases hw with ((h1 | h1) | h1) | h1 <;> subst h1 <;> simp
  omega

theorem readNat_natBytes (l : List UInt8) (i n : Nat) (rest : List UInt8)
    (hd : rem l i = natBytes n ++ rest)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readNat (ofList l) i = some (n, i + (natBytes n).length) := by
  have hws : skipWs (ofList l) i = i := by
    have := skipWs_append l i [] (natBytes n ++ rest) (by simpa using hd) (by simp)
      (fun c hc => by
        have : (natBytes n).head? = some c := by
          cases hn : natBytes n with
          | nil => exact absurd hn (natBytes_ne_nil n)
          | cons a as => rw [hn] at hc; simpa using hc
        exact natBytes_head_not_ws n c this)
    simpa using this
  have hscan := readNatAux_digits l (natBytes n) rest (natBytes_digits n) hstop i 0 hd
  simp [readNat, hws, hscan, dval_natBytes]
  intro hcontra
  exact absurd (hcontra ▸ rfl : (natBytes n).length = 0)
    (by simpa [List.length_eq_zero_iff] using natBytes_ne_nil n)

theorem ofList_get! (l : List UInt8) (i : Nat) (h : i < l.length) :
    (ofList l)[i]! = l[i] := by
  rw [getElem!_pos (ofList l) i (by simpa [ByteArray.size] using h)]
  exact ofList_get l i h

/-- Advance the cursor past a chunk: the workhorse for chaining fields. -/
theorem rem_add (l : List UInt8) (i : Nat) (A B : List UInt8) (h : rem l i = A ++ B) :
    rem l (i + A.length) = B := by
  have : rem l (i + A.length) = (rem l i).drop A.length := by
    simp [rem, List.drop_drop, Nat.add_comm]
  rw [this, h, List.drop_left]

/-- `readNat`, allowing the separators that precede every field after the first. -/
theorem readNat_ws (l : List UInt8) (i : Nat) (ws : List UInt8) (n : Nat) (rest : List UInt8)
    (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ natBytes n ++ rest)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readNat (ofList l) i = some (n, i + ws.length + (natBytes n).length) := by
  obtain ⟨a, as, hn⟩ : ∃ a as, natBytes n = a :: as := by
    cases hx : natBytes n with
    | nil => exact absurd hx (natBytes_ne_nil n)
    | cons a as => exact ⟨a, as, rfl⟩
  have hhead : ∀ c, (natBytes n ++ rest).head? = some c → ¬ isWs c := by
    intro c hc
    rw [hn] at hc
    simp only [List.cons_append, List.head?_cons, Option.some.injEq] at hc
    subst hc
    exact natBytes_head_not_ws n a (by rw [hn]; simp)
  have hskip : skipWs (ofList l) i = i + ws.length :=
    skipWs_append l i ws (natBytes n ++ rest) (by simpa [List.append_assoc] using hd) hws hhead
  have hrem : rem l (i + ws.length) = natBytes n ++ rest :=
    rem_add l i ws (natBytes n ++ rest) (by simpa [List.append_assoc] using hd)
  have hscan := readNatAux_digits l (natBytes n) rest (natBytes_digits n) hstop
                  (i + ws.length) 0 hrem
  have hpos : 0 < (natBytes n).length := by rw [hn]; simp
  simp only [readNat, hskip, hscan, dval_natBytes, Nat.zero_mul, Nat.zero_add]
  rw [if_neg (by simp only [beq_iff_eq]; omega)]

theorem intBytes_ofNat (n : Nat) : intBytes (Int.ofNat n) = natBytes n := rfl

theorem intBytes_negSucc (n : Nat) :
    intBytes (Int.negSucc n) = chMinus :: natBytes (n + 1) := rfl

/-- `readInt`.  The sign is one byte and the magnitude is a `natBytes`. -/
theorem readInt_ws (l : List UInt8) (i : Nat) (ws : List UInt8) (v : Int) (rest : List UInt8)
    (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ intBytes v ++ rest)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readInt (ofList l) i = some (v, i + ws.length + (intBytes v).length) := by
  cases v with
  | ofNat n =>
      rw [intBytes_ofNat] at hd ⊢
      obtain ⟨a, as, hn⟩ : ∃ a as, natBytes n = a :: as := by
        cases hx : natBytes n with
        | nil => exact absurd hx (natBytes_ne_nil n)
        | cons a as => exact ⟨a, as, rfl⟩
      have hhead : ∀ c, (natBytes n ++ rest).head? = some c → ¬ isWs c := by
        intro c hc
        rw [hn] at hc
        simp only [List.cons_append, List.head?_cons, Option.some.injEq] at hc
        subst hc
        exact natBytes_head_not_ws n a (by rw [hn]; simp)
      have hskip : skipWs (ofList l) i = i + ws.length :=
        skipWs_append l i ws (natBytes n ++ rest)
          (by simpa [List.append_assoc] using hd) hws hhead
      have hrem : rem l (i + ws.length) = natBytes n ++ rest :=
        rem_add l i ws (natBytes n ++ rest) (by simpa [List.append_assoc] using hd)
      have hlt : i + ws.length < l.length := by
        by_contra hge
        rw [rem_ge l _ (Nat.le_of_not_lt hge), hn] at hrem
        exact (List.cons_ne_nil a (as ++ rest)) hrem.symm
      have hbyte : l[i + ws.length] = a := by
        have hr := rem_lt l _ hlt
        rw [hr, hn, List.cons_append] at hrem
        exact (List.cons.inj hrem).1
      have hne : ¬ (l[i + ws.length] = (45 : UInt8)) := by
        have hdig := natBytes_digits n a (by rw [hn]; simp)
        simp only [isDigit, Bool.and_eq_true, decide_eq_true_eq,
                   UInt8.le_iff_toNat_le] at hdig
        rw [hbyte]
        intro hc
        rw [hc] at hdig
        simp at hdig
      have hnat := readNat_ws l (i + ws.length) [] n rest (by simp) (by simpa using hrem) hstop
      simp only [readInt, hskip, ofList_get! l _ hlt, hnat, List.length_nil, Nat.add_zero]
      rw [if_neg (by simp [hne])]
  | negSucc n =>
      rw [intBytes_negSucc] at hd ⊢
      have hskip : skipWs (ofList l) i = i + ws.length := by
        refine skipWs_append l i ws (chMinus :: natBytes (n + 1) ++ rest)
          (by simpa [List.append_assoc] using hd) hws ?_
        intro c hc
        simp only [List.cons_append, List.head?_cons, Option.some.injEq] at hc
        subst hc
        simp [isWs, chMinus]
      have hrem : rem l (i + ws.length) = chMinus :: (natBytes (n + 1) ++ rest) :=
        rem_add l i ws _ (by simpa [List.append_assoc] using hd)
      have hlt : i + ws.length < l.length := by
        by_contra hge
        rw [rem_ge l _ (Nat.le_of_not_lt hge)] at hrem
        exact (List.cons_ne_nil chMinus (natBytes (n + 1) ++ rest)) hrem.symm
      have hr := rem_lt l _ hlt
      have hbyte : l[i + ws.length] = chMinus := by
        rw [hr] at hrem; exact (List.cons.inj hrem).1
      have hrem1 : rem l (i + ws.length + 1) = natBytes (n + 1) ++ rest := by
        rw [hr] at hrem; exact (List.cons.inj hrem).2
      have hnat := readNat_ws l (i + ws.length + 1) [] (n + 1) rest (by simp)
                     (by simpa using hrem1) hstop
      simp only [readInt, hskip, ofList_get! l _ hlt, hbyte, chMinus, List.length_cons,
                 List.length_nil, Nat.add_zero, hnat]
      rw [if_pos (by simp [hlt])]
      simp
      omega

--------------------------------------------------------------------------------
-- 3.  Records
--------------------------------------------------------------------------------

theorem not_digit_space : ¬ isDigit chSpace := by decide
theorem not_digit_nl    : ¬ isDigit chNL    := by decide
theorem isWs_space      : isWs chSpace      := by decide
theorem isWs_nl         : isWs chNL         := by decide

/-- Whatever follows a record body begins with its newline, so the last field of
a record stops for the same reason every other field does. -/
theorem stop_space (t : List UInt8) : ∀ c, (chSpace :: t).head? = some c → ¬ isDigit c := by
  intro c hc; simp only [List.head?_cons, Option.some.injEq] at hc; subst hc; exact not_digit_space

theorem stop_nl (t : List UInt8) : ∀ c, (chNL :: t).head? = some c → ¬ isDigit c := by
  intro c hc; simp only [List.head?_cons, Option.some.injEq] at hc; subst hc; exact not_digit_nl

theorem ws_space : ∀ c ∈ [chSpace], isWs c := by
  intro c hc; simp only [List.mem_singleton] at hc; subst hc; exact isWs_space

theorem ws_nil : ∀ c ∈ ([] : List UInt8), isWs c := by intro c hc; simp at hc

/-- The two shapes every field actually takes: at the cursor, or one separator
along.  Stating them once keeps the record proofs free of `[].length` noise. -/
theorem readNat_here (l : List UInt8) (i n : Nat) (rest : List UInt8)
    (hd : rem l i = natBytes n ++ rest)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readNat (ofList l) i = some (n, i + (natBytes n).length) := by
  simpa using readNat_ws l i [] n rest ws_nil (by simpa using hd) hstop

theorem readNat_sep (l : List UInt8) (i n : Nat) (rest : List UInt8)
    (hd : rem l i = chSpace :: (natBytes n ++ rest))
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readNat (ofList l) i = some (n, i + 1 + (natBytes n).length) := by
  simpa using readNat_ws l i [chSpace] n rest ws_space (by simpa using hd) hstop

theorem readInt_here (l : List UInt8) (i : Nat) (v : Int) (rest : List UInt8)
    (hd : rem l i = intBytes v ++ rest)
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readInt (ofList l) i = some (v, i + (intBytes v).length) := by
  simpa using readInt_ws l i [] v rest ws_nil (by simpa using hd) hstop

theorem readInt_sep (l : List UInt8) (i : Nat) (v : Int) (rest : List UInt8)
    (hd : rem l i = chSpace :: (intBytes v ++ rest))
    (hstop : ∀ c, rest.head? = some c → ¬ isDigit c) :
    readInt (ofList l) i = some (v, i + 1 + (intBytes v).length) := by
  simpa using readInt_ws l i [chSpace] v rest ws_space (by simpa using hd) hstop

theorem rem_cons (l : List UInt8) (i : Nat) (c : UInt8) (t : List UInt8)
    (h : rem l i = c :: t) : rem l (i + 1) = t := by
  simpa using rem_add l i [c] t (by simpa using h)

theorem readOutput_outBody (l : List UInt8) (i : Nat) (o : OutputDesc) (rest : List UInt8)
    (hd : rem l i = outBody o ++ chNL :: rest) :
    readOutput (ofList l) i = some (o, i + (outBody o).length) := by
  have hd' : rem l i = natBytes o.slot ++ chSpace :: (natBytes o.width ++ chNL :: rest) := by
    simpa [outBody, spc, List.append_assoc] using hd
  have h1 := readNat_here l i o.slot _ hd' (stop_space _)
  have hr1 := rem_add l i (natBytes o.slot) _ hd'
  have h2 := readNat_sep l (i + (natBytes o.slot).length) o.width (chNL :: rest) hr1 (stop_nl _)
  have hlen : (outBody o).length = (natBytes o.slot).length + 1 + (natBytes o.width).length := by
    simp [outBody, spc, List.length_append, List.length_cons]; omega
  simp [readOutput, h1, h2, hlen]
  omega

theorem readMemory_memBody (l : List UInt8) (i : Nat) (m : MemoryDesc) (rest : List UInt8)
    (hd : rem l i = memBody m ++ chNL :: rest) :
    readMemory (ofList l) i = some (m, i + (memBody m).length) := by
  have hd' : rem l i = natBytes m.aw ++ chSpace :: (natBytes m.dw ++ chSpace ::
      (natBytes m.nextImg ++ chNL :: rest)) := by
    simpa [memBody, spc, List.append_assoc] using hd
  have h1 := readNat_here l i m.aw _ hd' (stop_space _)
  have hr1 := rem_add l i (natBytes m.aw) _ hd'
  have h2 := readNat_sep l (i + (natBytes m.aw).length) m.dw _ hr1 (stop_space _)
  have hr2 := rem_add l (i + (natBytes m.aw).length + 1) (natBytes m.dw) _
                (rem_cons l _ _ _ hr1)
  have h3 := readNat_sep l (i + (natBytes m.aw).length + 1 + (natBytes m.dw).length)
               m.nextImg (chNL :: rest) hr2 (stop_nl _)
  have hlen : (memBody m).length
      = (natBytes m.aw).length + 1 + (natBytes m.dw).length + 1 + (natBytes m.nextImg).length := by
    simp [memBody, spc, List.length_append, List.length_cons]; omega
  simp [readMemory, h1, h2, h3, hlen]
  omega

--------------------------------------------------------------------------------
-- 4.  The compositional interface
--------------------------------------------------------------------------------

/-- `f` at `i` decodes `x` and leaves exactly `R` unread.

The LEFTOVER SUFFIX, not an absolute cursor.  A record reader stops ON the
record's newline, so the chunk it consumes is one byte shorter than the chunk
rendered; phrasing the result as a position forces `Nat` subtraction and an
empty-section special case, while the suffix is all any caller needs.  Absolute
positions survive only as internal bridge facts inside each proof. -/
def ReadsTo {α : Type} (f : ByteArray → Nat → Option (α × Nat))
    (l : List UInt8) (i : Nat) (x : α) (R : List UInt8) : Prop :=
  ∃ j, f (ofList l) i = some (x, j) ∧ rem l j = R

/-- What follows a rendered field is always a separator, never a digit. -/
theorem stop_of_sep (t : List UInt8) (c₀ : UInt8) (h : c₀ = chSpace ∨ c₀ = chNL) :
    ∀ c, (c₀ :: t).head? = some c → ¬ isDigit c := by
  intro c hc
  simp only [List.head?_cons, Option.some.injEq] at hc
  subst hc
  rcases h with h | h <;> subst h
  · exact not_digit_space
  · exact not_digit_nl

/-- The contents of a `memConst` are space-prefixed, so the head of what follows
any one of them is a space or the record's newline. -/
theorem head_ints_or_nl (vs : List Int) (rest : List UInt8) :
    ∀ c, ((vs.map (fun v => chSpace :: intBytes v)).flatten ++ chNL :: rest).head? = some c →
      ¬ isDigit c := by
  cases vs with
  | nil => intro c hc; simpa using stop_nl rest c (by simpa using hc)
  | cons v vs => intro c hc; simp only [List.map_cons, List.flatten_cons, List.cons_append,
                                         List.head?_cons, Option.some.injEq] at hc
                 subst hc; exact not_digit_space

/-- `readMany` over a space-prefixed run of values, terminated by a separator.
Both variable-length arrays in the format have this shape: a `memConst`'s
contents (terminated by the record's newline) and a node's dependency list
(terminated by the space before `origin`). -/
theorem head_sep_run {β : Type} (g : β → List UInt8) (xs : List β) (c₀ : UInt8)
    (T : List UInt8) (hc : c₀ = chSpace ∨ c₀ = chNL) :
    ∀ c, ((xs.map (fun x => chSpace :: g x)).flatten ++ c₀ :: T).head? = some c → ¬ isDigit c := by
  cases xs with
  | nil => intro c hc'; exact stop_of_sep T c₀ hc c (by simpa using hc')
  | cons x xs =>
      intro c hc'
      simp only [List.map_cons, List.flatten_cons, List.cons_append, List.head?_cons,
                 Option.some.injEq] at hc'
      subst hc'; exact not_digit_space

theorem readMany_ints (l : List UInt8) :
    ∀ (vs : List Int) (i : Nat) (acc : Array Int) (c₀ : UInt8) (T : List UInt8),
      (c₀ = chSpace ∨ c₀ = chNL) →
      rem l i = (vs.map (fun v => chSpace :: intBytes v)).flatten ++ c₀ :: T →
      ∃ j, readMany readInt (ofList l) i vs.length acc = some (acc ++ vs.toArray, j) ∧
           rem l j = c₀ :: T := by
  intro vs
  induction vs with
  | nil => intro i acc c₀ T _ hd; exact ⟨i, by simp [readMany], by simpa using hd⟩
  | cons v vs ih =>
      intro i acc c₀ T hc hd
      have hd' : rem l i = chSpace ::
          (intBytes v ++ ((vs.map (fun v => chSpace :: intBytes v)).flatten ++ c₀ :: T)) := by
        simpa [List.append_assoc] using hd
      have h1 := readInt_sep l i v _ hd' (head_sep_run intBytes vs c₀ T hc)
      have hr1 : rem l (i + 1 + (intBytes v).length)
          = (vs.map (fun v => chSpace :: intBytes v)).flatten ++ c₀ :: T :=
        rem_add l (i + 1) (intBytes v) _ (rem_cons l i chSpace _ hd')
      obtain ⟨j, hj, hjr⟩ := ih (i + 1 + (intBytes v).length) (acc.push v) c₀ T hc hr1
      refine ⟨j, ?_, hjr⟩
      simp only [List.length_cons, readMany, h1, hj]
      simp

theorem readMany_nats (l : List UInt8) :
    ∀ (ds : List Nat) (i : Nat) (acc : Array Nat) (c₀ : UInt8) (T : List UInt8),
      (c₀ = chSpace ∨ c₀ = chNL) →
      rem l i = (ds.map (fun d => chSpace :: natBytes d)).flatten ++ c₀ :: T →
      ∃ j, readMany readNat (ofList l) i ds.length acc = some (acc ++ ds.toArray, j) ∧
           rem l j = c₀ :: T := by
  intro ds
  induction ds with
  | nil => intro i acc c₀ T _ hd; exact ⟨i, by simp [readMany], by simpa using hd⟩
  | cons d ds ih =>
      intro i acc c₀ T hc hd
      have hd' : rem l i = chSpace ::
          (natBytes d ++ ((ds.map (fun d => chSpace :: natBytes d)).flatten ++ c₀ :: T)) := by
        simpa [List.append_assoc] using hd
      have h1 := readNat_sep l i d _ hd' (head_sep_run natBytes ds c₀ T hc)
      have hr1 : rem l (i + 1 + (natBytes d).length)
          = (ds.map (fun d => chSpace :: natBytes d)).flatten ++ c₀ :: T :=
        rem_add l (i + 1) (natBytes d) _ (rem_cons l i chSpace _ hd')
      obtain ⟨j, hj, hjr⟩ := ih (i + 1 + (natBytes d).length) (acc.push d) c₀ T hc hr1
      refine ⟨j, ?_, hjr⟩
      simp only [List.length_cons, readMany, h1, hj]
      simp

/-- `readMany` over rendered record chunks: each is a body plus its newline, and
the reader stops on that newline, which the next iteration's `skipWs` eats. -/
theorem readMany_chunks {α : Type} (f : ByteArray → Nat → Option (α × Nat))
    (body : α → List UInt8) (l : List UInt8)
    (hf : ∀ (i : Nat) (ws : List UInt8) (x : α) (rest : List UInt8),
            (∀ c ∈ ws, isWs c) → rem l i = ws ++ body x ++ chNL :: rest →
            ReadsTo f l i x (chNL :: rest)) :
    ∀ (xs : List α) (ws : List UInt8) (i : Nat) (acc : Array α) (R : List UInt8),
      (∀ c ∈ ws, isWs c) →
      rem l i = ws ++ (xs.map (fun x => body x ++ [chNL])).flatten ++ R →
      ∃ j, readMany f (ofList l) i xs.length acc = some (acc ++ xs.toArray, j) ∧
           rem l j = (if xs.isEmpty then ws ++ R else chNL :: R) := by
  intro xs
  induction xs with
  | nil =>
      intro ws i acc R hws hd
      exact ⟨i, by simp [readMany], by simpa using hd⟩
  | cons x xs ih =>
      intro ws i acc R hws hd
      have hd' : rem l i = ws ++ body x ++ chNL ::
          ((xs.map (fun x => body x ++ [chNL])).flatten ++ R) := by
        simpa [List.append_assoc] using hd
      obtain ⟨j₀, hj₀, hj₀r⟩ := hf i ws x _ hws hd'
      obtain ⟨j, hj, hjr⟩ := ih [chNL] j₀ (acc.push x) R (by
        intro c hc; simp only [List.mem_singleton] at hc; subst hc; exact isWs_nl)
        (by simpa using hj₀r)
      refine ⟨j, ?_, ?_⟩
      · simp only [List.length_cons, readMany, hj₀, hj]
        simp
      · simpa using hjr

/-- A whole length-prefixed section.  The leftover is `chNL :: R` whether or not
the section is empty, which is what makes the top-level chain uniform. -/
theorem readSection {α : Type} (f : ByteArray → Nat → Option (α × Nat))
    (body : α → List UInt8) (l : List UInt8)
    (hf : ∀ (i : Nat) (ws : List UInt8) (x : α) (rest : List UInt8),
            (∀ c ∈ ws, isWs c) → rem l i = ws ++ body x ++ chNL :: rest →
            ReadsTo f l i x (chNL :: rest))
    (chunk : α → List UInt8) (hchunk : ∀ x, chunk x = body x ++ [chNL])
    (xs : Array α) (ws : List UInt8) (i : Nat) (R : List UInt8)
    (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ natBytes xs.size ++ chNL ::
            ((xs.toList.map chunk).flatten ++ R)) :
    ∃ j k, readNat (ofList l) i = some (xs.size, k) ∧
           readMany f (ofList l) k xs.size #[] = some (xs, j) ∧
           rem l j = chNL :: R := by
  have hmap : (xs.toList.map chunk).flatten
      = (xs.toList.map (fun x => body x ++ [chNL])).flatten := by
    congr 1
    exact List.map_congr_left (fun x _ => hchunk x)
  have hd' : rem l i = ws ++ natBytes xs.size ++ chNL ::
      ((xs.toList.map (fun x => body x ++ [chNL])).flatten ++ R) := by rw [hd, hmap]
  have h1 := readNat_ws l i ws xs.size _ hws (by simpa [List.append_assoc] using hd') (stop_nl _)
  have hr1 : rem l (i + ws.length + (natBytes xs.size).length)
      = chNL :: ((xs.toList.map (fun x => body x ++ [chNL])).flatten ++ R) :=
    rem_add l (i + ws.length) (natBytes xs.size) _
      (rem_add l i ws _ (by simpa [List.append_assoc] using hd'))
  obtain ⟨j, hj, hjr⟩ := readMany_chunks f body l hf xs.toList [chNL]
    (i + ws.length + (natBytes xs.size).length) #[] R
    (by intro c hc; simp only [List.mem_singleton] at hc; subst hc; exact isWs_nl)
    (by simpa using hr1)
  refine ⟨j, i + ws.length + (natBytes xs.size).length, h1, ?_, ?_⟩
  · simpa using hj
  · cases hxs : xs.toList with
    | nil => simpa [hxs] using hjr
    | cons a as => simpa [hxs] using hjr

--------------------------------------------------------------------------------
-- 5.  Field combinators
--------------------------------------------------------------------------------

/-- The first field of a record: preceded by whatever whitespace separated it
from the previous record, followed by a separator. -/
theorem headNat (l : List UInt8) (p : Nat) (ws : List UInt8) (n : Nat) (T : List UInt8)
    (c₀ : UInt8) (hws : ∀ c ∈ ws, isWs c) (hc : c₀ = chSpace ∨ c₀ = chNL)
    (h : rem l p = ws ++ natBytes n ++ c₀ :: T) :
    readNat (ofList l) p = some (n, p + ws.length + (natBytes n).length)
    ∧ rem l (p + ws.length + (natBytes n).length) = c₀ :: T := by
  refine ⟨readNat_ws l p ws n _ hws (by simpa [List.append_assoc] using h)
            (stop_of_sep T c₀ hc), ?_⟩
  exact rem_add l (p + ws.length) (natBytes n) _
    (rem_add l p ws _ (by simpa [List.append_assoc] using h))

/-- Every later field: one separator, then the number. -/
theorem fieldNat (l : List UInt8) (p n : Nat) (T : List UInt8) (c₀ : UInt8)
    (hc : c₀ = chSpace ∨ c₀ = chNL) (h : rem l p = chSpace :: (natBytes n ++ c₀ :: T)) :
    readNat (ofList l) p = some (n, p + 1 + (natBytes n).length)
    ∧ rem l (p + 1 + (natBytes n).length) = c₀ :: T := by
  refine ⟨readNat_sep l p n _ h (stop_of_sep T c₀ hc), ?_⟩
  exact rem_add l (p + 1) (natBytes n) _ (rem_cons l p chSpace _ h)

theorem fieldInt (l : List UInt8) (p : Nat) (v : Int) (T : List UInt8) (c₀ : UInt8)
    (hc : c₀ = chSpace ∨ c₀ = chNL) (h : rem l p = chSpace :: (intBytes v ++ c₀ :: T)) :
    readInt (ofList l) p = some (v, p + 1 + (intBytes v).length)
    ∧ rem l (p + 1 + (intBytes v).length) = c₀ :: T := by
  refine ⟨readInt_sep l p v _ h (stop_of_sep T c₀ hc), ?_⟩
  exact rem_add l (p + 1) (intBytes v) _ (rem_cons l p chSpace _ h)

/-- Variants taking a PREDICATE on the tail instead of an explicit `c₀ :: T`.
A count field is followed by a run that may be empty, so its successor byte is
not syntactically available; `head_sep_run` supplies the predicate directly. -/
theorem fieldNat' (l : List UInt8) (p n : Nat) (U : List UInt8)
    (hU : ∀ c, U.head? = some c → ¬ isDigit c)
    (h : rem l p = chSpace :: (natBytes n ++ U)) :
    readNat (ofList l) p = some (n, p + 1 + (natBytes n).length)
    ∧ rem l (p + 1 + (natBytes n).length) = U := by
  refine ⟨readNat_sep l p n _ h hU, ?_⟩
  exact rem_add l (p + 1) (natBytes n) _ (rem_cons l p chSpace _ h)

--------------------------------------------------------------------------------
-- 6.  Operator payloads
--------------------------------------------------------------------------------

/-- All 29 constructors, including the three that carry a payload. -/
theorem opOfCode_opCode (op : LGraphOp) : opOfCode (opCode op).1 (opCode op).2 = some op := by
  cases op <;> simp [opCode, opOfCode]

--------------------------------------------------------------------------------
-- 7.  Record readers
--------------------------------------------------------------------------------

theorem readOutput_reads (l : List UInt8) (i : Nat) (ws : List UInt8) (o : OutputDesc)
    (rest : List UInt8) (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ outBody o ++ chNL :: rest) :
    ReadsTo readOutput l i o (chNL :: rest) := by
  have hd' : rem l i = ws ++ natBytes o.slot ++ chSpace :: (natBytes o.width ++ chNL :: rest) := by
    simpa [outBody, spc, List.append_assoc] using hd
  obtain ⟨h1, r1⟩ := headNat l i ws o.slot _ chSpace hws (Or.inl rfl) (by simpa using hd')
  obtain ⟨h2, r2⟩ := fieldNat l _ o.width rest chNL (Or.inr rfl) (by simpa using r1)
  exact ⟨_, by simp [readOutput, h1, h2], r2⟩

theorem readMemory_reads (l : List UInt8) (i : Nat) (ws : List UInt8) (m : MemoryDesc)
    (rest : List UInt8) (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ memBody m ++ chNL :: rest) :
    ReadsTo readMemory l i m (chNL :: rest) := by
  have hd' : rem l i = ws ++ natBytes m.aw ++ chSpace :: (natBytes m.dw ++ chSpace ::
      (natBytes m.nextImg ++ chNL :: rest)) := by
    simpa [memBody, spc, List.append_assoc] using hd
  obtain ⟨h1, r1⟩ := headNat l i ws m.aw _ chSpace hws (Or.inl rfl) (by simpa using hd')
  obtain ⟨h2, r2⟩ := fieldNat l _ m.dw _ chSpace (Or.inl rfl) (by simpa using r1)
  obtain ⟨h3, r3⟩ := fieldNat l _ m.nextImg rest chNL (Or.inr rfl) (by simpa using r2)
  exact ⟨_, by simp [readMemory, h1, h2, h3], r3⟩

/-- The presence flags, pulled out so the field chain runs once instead of four
times.  Both optional fields encode as a flag plus a slot, and the decode
`if he != 0 then some e else none` has to invert that. -/
theorem flopBody_shape (f : FlopDesc) :
    ∃ he e hr r : Nat,
      ((f.enable = none ∧ he = 0 ∧ e = 0) ∨ (∃ x, f.enable = some x ∧ he = 1 ∧ e = x)) ∧
      ((f.resetPin = none ∧ hr = 0 ∧ r = 0) ∨ (∃ x, f.resetPin = some x ∧ hr = 1 ∧ r = x)) ∧
      flopBody f = natBytes f.width ++ spc (natBytes f.din) ++ spc (natBytes he)
        ++ spc (natBytes e) ++ spc (natBytes hr) ++ spc (natBytes r)
        ++ spc (intBytes f.resetValue)
        ++ spc (natBytes (if f.resetActiveLow then 1 else 0)) := by
  cases hen : f.enable <;> cases hrp : f.resetPin
  · exact ⟨0, 0, 0, 0, Or.inl ⟨rfl, rfl, rfl⟩, Or.inl ⟨rfl, rfl, rfl⟩,
      by simp [flopBody, hen, hrp]⟩
  · next x => exact ⟨0, 0, 1, x, Or.inl ⟨rfl, rfl, rfl⟩, Or.inr ⟨x, rfl, rfl, rfl⟩,
      by simp [flopBody, hen, hrp]⟩
  · next x => exact ⟨1, x, 0, 0, Or.inr ⟨x, rfl, rfl, rfl⟩, Or.inl ⟨rfl, rfl, rfl⟩,
      by simp [flopBody, hen, hrp]⟩
  · next x y => exact ⟨1, x, 1, y, Or.inr ⟨x, rfl, rfl, rfl⟩, Or.inr ⟨y, rfl, rfl, rfl⟩,
      by simp [flopBody, hen, hrp]⟩

theorem readFlop_reads (l : List UInt8) (i : Nat) (ws : List UInt8) (f : FlopDesc)
    (rest : List UInt8) (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ flopBody f ++ chNL :: rest) :
    ReadsTo readFlop l i f (chNL :: rest) := by
  obtain ⟨he, e, hr, r, hEn, hRp, hBody⟩ := flopBody_shape f
  rw [hBody] at hd
  have hd' : rem l i = ws ++ natBytes f.width ++ chSpace :: (natBytes f.din ++ chSpace ::
      (natBytes he ++ chSpace :: (natBytes e ++ chSpace :: (natBytes hr ++ chSpace ::
      (natBytes r ++ chSpace :: (intBytes f.resetValue ++ chSpace ::
      (natBytes (if f.resetActiveLow then 1 else 0) ++ chNL :: rest))))))) := by
    simpa [spc, List.append_assoc] using hd
  obtain ⟨h1, r1⟩ := headNat l i ws f.width _ chSpace hws (Or.inl rfl) (by simpa using hd')
  obtain ⟨h2, r2⟩ := fieldNat l _ f.din _ chSpace (Or.inl rfl) (by simpa using r1)
  obtain ⟨h3, r3⟩ := fieldNat l _ he _ chSpace (Or.inl rfl) (by simpa using r2)
  obtain ⟨h4, r4⟩ := fieldNat l _ e _ chSpace (Or.inl rfl) (by simpa using r3)
  obtain ⟨h5, r5⟩ := fieldNat l _ hr _ chSpace (Or.inl rfl) (by simpa using r4)
  obtain ⟨h6, r6⟩ := fieldNat l _ r _ chSpace (Or.inl rfl) (by simpa using r5)
  obtain ⟨h7, r7⟩ := fieldInt l _ f.resetValue _ chSpace (Or.inl rfl) (by simpa using r6)
  obtain ⟨h8, r8⟩ := fieldNat l _ (if f.resetActiveLow then 1 else 0) rest chNL (Or.inr rfl)
    (by simpa using r7)
  refine ⟨_, ?_, r8⟩
  simp [readFlop, h1, h2, h3, h4, h5, h6, h7, h8]
  -- what is left is the record equality: the presence flag plus slot must invert
  -- back to the `Option`, for all four combinations, and the polarity byte to
  -- the Bool.
  have hLow : ((if f.resetActiveLow then (1 : Nat) else 0) != 0) = f.resetActiveLow := by
    cases hb : f.resetActiveLow <;> simp [hb]
  rcases hEn with ⟨hE, hE0, hE1⟩ | ⟨x, hE, hE0, hE1⟩ <;>
    rcases hRp with ⟨hR, hR0, hR1⟩ | ⟨y, hR, hR0, hR1⟩ <;>
    subst hE0 <;> subst hE1 <;> subst hR0 <;> subst hR1 <;>
    cases f <;> simp_all

/-- A value run is never empty where it matters: whatever follows the count is a
separator, whether the run has elements or not. -/
theorem run_head {β : Type} (g : β → List UInt8) (xs : List β) (c₀ : UInt8) (T : List UInt8)
    (hc : c₀ = chSpace ∨ c₀ = chNL) :
    ∃ c₁ T₁, (xs.map (fun x => chSpace :: g x)).flatten ++ c₀ :: T = c₁ :: T₁
             ∧ (c₁ = chSpace ∨ c₁ = chNL) := by
  cases xs with
  | nil => exact ⟨c₀, T, by simp, hc⟩
  | cons x xs =>
      exact ⟨chSpace, g x ++ ((xs.map (fun x => chSpace :: g x)).flatten ++ c₀ :: T),
             by simp, Or.inl rfl⟩

theorem readSource_reads (l : List UInt8) (i : Nat) (ws : List UInt8) (s : SourceDesc)
    (rest : List UInt8) (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ srcBody s ++ chNL :: rest) :
    ReadsTo readSource l i s (chNL :: rest) := by
  cases s with
  | input idx w =>
      have hd' : rem l i = ws ++ natBytes 0 ++ chSpace :: (natBytes idx ++ chSpace ::
          (natBytes w ++ chNL :: rest)) := by simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 0 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ idx _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldNat l _ w rest chNL (Or.inr rfl) (by simpa using r2)
      exact ⟨_, by simp [readSource, h1, h2, h3], r3⟩
  | const w v =>
      have hd' : rem l i = ws ++ natBytes 1 ++ chSpace :: (natBytes w ++ chSpace ::
          (intBytes v ++ chNL :: rest)) := by simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 1 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ w _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldInt l _ v rest chNL (Or.inr rfl) (by simpa using r2)
      exact ⟨_, by simp [readSource, h1, h2, h3], r3⟩
  | flopQ idx w =>
      have hd' : rem l i = ws ++ natBytes 2 ++ chSpace :: (natBytes idx ++ chSpace ::
          (natBytes w ++ chNL :: rest)) := by simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 2 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ idx _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldNat l _ w rest chNL (Or.inr rfl) (by simpa using r2)
      exact ⟨_, by simp [readSource, h1, h2, h3], r3⟩
  | flopQAsync idx w ri rv a =>
      have hd' : rem l i = ws ++ natBytes 3 ++ chSpace :: (natBytes idx ++ chSpace ::
          (natBytes w ++ chSpace :: (natBytes ri ++ chSpace :: (intBytes rv ++ chSpace ::
          (natBytes (if a then 1 else 0) ++ chNL :: rest))))) := by
        simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 3 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ idx _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldNat l _ w _ chSpace (Or.inl rfl) (by simpa using r2)
      obtain ⟨h4, r4⟩ := fieldNat l _ ri _ chSpace (Or.inl rfl) (by simpa using r3)
      obtain ⟨h5, r5⟩ := fieldInt l _ rv _ chSpace (Or.inl rfl) (by simpa using r4)
      obtain ⟨h6, r6⟩ := fieldNat l _ (if a then 1 else 0) rest chNL (Or.inr rfl)
        (by simpa using r5)
      refine ⟨_, ?_, r6⟩
      cases a <;> simp [readSource, h1, h2, h3, h4, h5, h6]
  | memImg idx aw dw =>
      have hd' : rem l i = ws ++ natBytes 4 ++ chSpace :: (natBytes idx ++ chSpace ::
          (natBytes aw ++ chSpace :: (natBytes dw ++ chNL :: rest))) := by
        simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 4 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ idx _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldNat l _ aw _ chSpace (Or.inl rfl) (by simpa using r2)
      obtain ⟨h4, r4⟩ := fieldNat l _ dw rest chNL (Or.inr rfl) (by simpa using r3)
      exact ⟨_, by simp [readSource, h1, h2, h3, h4], r4⟩
  | memConst aw dw cs =>
      -- the contents are a space-prefixed run that may be EMPTY, so the byte
      -- after the count is a space or the record's newline; `head_sep_run`
      -- states that without naming it
      have hd' : rem l i = ws ++ natBytes 5 ++ chSpace :: (natBytes aw ++ chSpace ::
          (natBytes dw ++ chSpace :: (natBytes cs.size ++
            ((cs.toList.map (fun v => chSpace :: intBytes v)).flatten ++ chNL :: rest)))) := by
        simpa [srcBody, spc, List.append_assoc] using hd
      obtain ⟨h1, r1⟩ := headNat l i ws 5 _ chSpace hws (Or.inl rfl) (by simpa using hd')
      obtain ⟨h2, r2⟩ := fieldNat l _ aw _ chSpace (Or.inl rfl) (by simpa using r1)
      obtain ⟨h3, r3⟩ := fieldNat l _ dw _ chSpace (Or.inl rfl) (by simpa using r2)
      obtain ⟨h4, r4⟩ := fieldNat' l _ cs.size
        ((cs.toList.map (fun v => chSpace :: intBytes v)).flatten ++ chNL :: rest)
        (head_sep_run intBytes cs.toList chNL rest (Or.inr rfl)) (by simpa using r3)
      obtain ⟨j, hj, hjr⟩ := readMany_ints l cs.toList _ #[] chNL rest (Or.inr rfl)
        (by simpa using r4)
      refine ⟨j, ?_, hjr⟩
      rw [Array.length_toList] at hj
      simp [readSource, h1, h2, h3, h4]
      erw [hj]
      simp

theorem readNode_reads (l : List UInt8) (i : Nat) (ws : List UInt8) (n : DenseNodeCert)
    (rest : List UInt8) (hws : ∀ c ∈ ws, isWs c)
    (hd : rem l i = ws ++ nodeBody n ++ chNL :: rest) :
    ReadsTo readNode l i n (chNL :: rest) := by
  have hd' : rem l i = ws ++ natBytes (opCode n.op).1 ++ chSpace ::
      (intBytes (opCode n.op).2 ++ chSpace :: (natBytes n.width ++ chSpace ::
      (natBytes n.deps.size ++ ((n.deps.toList.map (fun d => chSpace :: natBytes d)).flatten
        ++ chSpace :: (natBytes n.origin ++ chNL :: rest))))) := by
    simpa [nodeBody, spc, List.append_assoc] using hd
  obtain ⟨h1, r1⟩ := headNat l i ws (opCode n.op).1 _ chSpace hws (Or.inl rfl) (by simpa using hd')
  obtain ⟨h2, r2⟩ := fieldInt l _ (opCode n.op).2 _ chSpace (Or.inl rfl) (by simpa using r1)
  obtain ⟨h3, r3⟩ := fieldNat l _ n.width _ chSpace (Or.inl rfl) (by simpa using r2)
  obtain ⟨h4, r4⟩ := fieldNat' l _ n.deps.size
    ((n.deps.toList.map (fun d => chSpace :: natBytes d)).flatten
      ++ chSpace :: (natBytes n.origin ++ chNL :: rest))
    (head_sep_run natBytes n.deps.toList chSpace (natBytes n.origin ++ chNL :: rest) (Or.inl rfl))
    (by simpa using r3)
  obtain ⟨j₀, hj₀, hj₀r⟩ := readMany_nats l n.deps.toList _ #[] chSpace
    (natBytes n.origin ++ chNL :: rest) (Or.inl rfl) (by simpa using r4)
  obtain ⟨h6, r6⟩ := fieldNat l j₀ n.origin rest chNL (Or.inr rfl) (by simpa using hj₀r)
  refine ⟨_, ?_, r6⟩
  rw [Array.length_toList] at hj₀
  simp [readNode, h1, h2, h3, h4]
  erw [hj₀]
  simp [h6, opOfCode_opCode]

--------------------------------------------------------------------------------
-- 8.  The whole certificate
--------------------------------------------------------------------------------

/-- Indexing inside a known prefix. -/
theorem getElem!_prefix (pre t : List UInt8) (k : Nat) (h : k < pre.length) :
    (ofList (pre ++ t))[k]! = pre[k]! := by
  have h1 : k < (pre ++ t).length := by simp; omega
  rw [getElem!_pos _ _ (by simpa [ByteArray.size] using h1), getElem!_pos pre k h,
      ofList_get _ _ h1, List.getElem_append_left h]

theorem certBytes_magic (D : DesignCert) :
    certBytes D = magicBytes ++ (section' srcBytes D.sources ++ section' nodeBytes D.nodes
      ++ section' outBytes D.outputs ++ section' flopBytes D.flops
      ++ section' memBytes D.memories) := by
  simp [certBytes, List.append_assoc]

theorem hasMagic_render (D : DesignCert) : hasMagic (renderCert D) = true := by
  have hb : renderCert D = ofList (magicBytes ++ (section' srcBytes D.sources
      ++ section' nodeBytes D.nodes ++ section' outBytes D.outputs
      ++ section' flopBytes D.flops ++ section' memBytes D.memories)) := by
    rw [renderCert, certBytes_magic]
  set t := section' srcBytes D.sources ++ section' nodeBytes D.nodes
      ++ section' outBytes D.outputs ++ section' flopBytes D.flops
      ++ section' memBytes D.memories with ht
  rw [hb, hasMagic]
  have hsz : (ofList (magicBytes ++ t)).size ≥ ("DCERT1".toUTF8).size := by
    rw [show "DCERT1".toUTF8.size = 6 from rfl]; simp [magicBytes]
  refine Bool.and_eq_true _ _ |>.mpr ⟨by simpa using hsz, ?_⟩
  simp only [List.all_eq_true, List.mem_range]
  intro k hk
  rw [show "DCERT1".toUTF8.size = 6 from rfl] at hk
  have hk6 : k < magicBytes.length := by simp [magicBytes]; omega
  rw [getElem!_prefix magicBytes t k hk6]
  interval_cases k <;> rfl

/-- Only the record's newline is left, so skipping it reaches exactly the end --
which is what `parseCert`'s trailing check demands. -/
theorem skipWs_final (l : List UInt8) (j : Nat) (h : rem l j = [chNL]) :
    skipWs (ofList l) j = (ofList l).size := by
  have h2 : j < l.length := by
    by_contra hc
    rw [rem_ge l j (by omega)] at h
    exact (List.cons_ne_nil chNL []) h.symm
  have hlen : j + 1 = l.length := by
    have h1 : (rem l j).length = l.length - j := by simp [rem]
    rw [h] at h1; simp at h1; omega
  have hsk := skipWs_append l j [chNL] [] (by simpa using h)
    (by intro c hc; simp only [List.mem_singleton] at hc; subst hc; exact isWs_nl)
    (by intro c hc; simp at hc)
  simp only [ofList_size]
  rw [hsk]; simpa using hlen

/-- **The round trip.**  Every `DesignCert`, no side conditions. -/
theorem parseCert_renderCert (D : DesignCert) : parseCert (renderCert D) = .ok D := by
  have hws : ∀ c ∈ [chNL], isWs c := by
    intro c hc; simp only [List.mem_singleton] at hc; subst hc; exact isWs_nl
  set l := certBytes D with hl
  have hstart : rem l 6 = [chNL] ++ (section' srcBytes D.sources
      ++ (section' nodeBytes D.nodes ++ (section' outBytes D.outputs
      ++ (section' flopBytes D.flops ++ (section' memBytes D.memories ++ []))))) := by
    show (certBytes D).drop 6 = _
    rw [certBytes_magic]
    simp [magicBytes, chNL, List.append_assoc]
  obtain ⟨j1, k1, hk1, hj1, hr1⟩ := readSection readSource srcBody l
    (fun i ws x rest h1 h2 => readSource_reads l i ws x rest h1 h2) srcBytes (fun _ => rfl)
    D.sources [chNL] 6 _ hws (by simpa [section', srcBytes, List.append_assoc] using hstart)
  obtain ⟨j2, k2, hk2, hj2, hr2⟩ := readSection readNode nodeBody l
    (fun i ws x rest h1 h2 => readNode_reads l i ws x rest h1 h2) nodeBytes (fun _ => rfl)
    D.nodes [chNL] j1 _ hws (by simpa [section', nodeBytes, List.append_assoc] using hr1)
  obtain ⟨j3, k3, hk3, hj3, hr3⟩ := readSection readOutput outBody l
    (fun i ws x rest h1 h2 => readOutput_reads l i ws x rest h1 h2) outBytes (fun _ => rfl)
    D.outputs [chNL] j2 _ hws (by simpa [section', outBytes, List.append_assoc] using hr2)
  obtain ⟨j4, k4, hk4, hj4, hr4⟩ := readSection readFlop flopBody l
    (fun i ws x rest h1 h2 => readFlop_reads l i ws x rest h1 h2) flopBytes (fun _ => rfl)
    D.flops [chNL] j3 _ hws (by simpa [section', flopBytes, List.append_assoc] using hr3)
  obtain ⟨j5, k5, hk5, hj5, hr5⟩ := readSection readMemory memBody l
    (fun i ws x rest h1 h2 => readMemory_reads l i ws x rest h1 h2) memBytes (fun _ => rfl)
    D.memories [chNL] j4 [] hws (by simpa [section', memBytes, List.append_assoc] using hr4)
  have hend : skipWs (ofList l) j5 = (ofList l).size := skipWs_final l j5 (by simpa using hr5)
  have hgo : parseCert.go (renderCert D) = some D := by
    show parseCert.go (ofList l) = some D
    simp [parseCert.go, hk1, hj1, hk2, hj2, hk3, hj3, hk4, hj4, hk5, hj5, hend]
  rw [parseCert, if_neg (by simp [hasMagic_render D]), hgo]

end CertIO
end Compiler
