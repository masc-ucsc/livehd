import Init

-- The source bit pattern 100000 has signed value -32 at width six.
def operand : BitVec 6 := BitVec.ofNat 6 32
-- Historical saved artifact, before constant-width correction.
def historical676 : Bool := decide (operand.toInt < (BitVec.ofInt 6 64).toInt)
-- Frozen baseline, using a common comparison width of seven.
def baseline676 : Bool := decide ((operand.setWidth 7).toInt < (BitVec.ofInt 7 64).toInt)
-- Current renderer, using each operand's signed width.
def current676 : Bool := decide (operand.toInt < (BitVec.ofInt 7 64).toInt)

theorem timeline : historical676 = true ∧ baseline676 = false ∧ current676 = false := by decide
-- A separate example where the renderer change itself changes the answer.
theorem mixed_width_renderer_difference :
  decide ((BitVec.ofNat 8 0).toInt < ((BitVec.ofNat 4 15).setWidth 8).toInt) = true ∧
  decide ((BitVec.ofNat 8 0).toInt < (BitVec.ofNat 4 15).toInt) = false := by decide
#print axioms timeline
#print axioms mixed_width_renderer_difference
