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

end CertIO
end Compiler
