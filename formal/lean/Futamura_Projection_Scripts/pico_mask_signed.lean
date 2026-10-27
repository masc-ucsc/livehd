/-
SIGNED vs UNSIGNED control for Pico's mask cone.

§63.64 established, kernel-checked, that the cone
`MuxBool w1 [sel,0,-1] -> GetMask w32 [mux, 0xffffffff]` yields mask **1**, so
the enabled write touches bit 0 only.  This file isolates WHY, and checks the
smallest adaptation that would make the certificate agree with the native
lowering -- WITHOUT changing the meaning of any operator.

The native lowering (`cgen_sim.cpp`, the `Get_mask` window case) says it
outright: "A window that reaches past the value's declared width selects its
SIGN bits.  For an UNSIGNED value those are all zero, so a zero-extended read
already reproduces them; only a signed value needs the sign actually
replicated", and it tests `is_unsign(e[0].get_driver_pin())`.

The certificate path cannot make that test: `design_scan.cpp`'s `capture_pin`
records a pin's `kind`, `id`, `port` and `width`, and NO signedness, so a
width-1 operand reaches Lean as a width-1 `BV` and `bv_get_mask` reads it as
the one bit it is.  That is the divergence -- a translation-boundary gap, not a
disagreement about what `Op_GetMask` means.

NOTHING here proposes changing `bv_get_mask`, `Op_SHL`, `Op_SRA` or `Op_Sext`.
The controls below show that widening the OPERAND by the existing, already
proved `Op_Sext` reproduces the native value exactly, which is why an inserted
`Sext` at the scan boundary is the smallest candidate adaptation.
-/
import LeanSemanticPrimitives.Translation.LGraphModel


namespace Audit.PicoMaskSigned

/-- What the CERTIFICATE computes today: a width-1 operand under a 32-bit
window gives one bit. -/
theorem cert_narrow :
    eval_op .Op_GetMask 32 [mk_bv 1 1, mk_bv 32 4294967295] = mk_bv 32 1 := by
  decide +kernel

/-- What the NATIVE lowering computes: the SIGNED operand is read at a carrier
wide enough for the window, so its sign is replicated first. -/
theorem native_signed :
    eval_op .Op_GetMask 32 [mk_bv 32 (-1), mk_bv 32 4294967295]
      = mk_bv 32 4294967295 := by
  decide +kernel

/-- …and the UNSIGNED control, which is the half the native code says needs
nothing: a zero-extended width-1 one still selects one bit, so the two paths
AGREE whenever the operand is unsigned.  The divergence is exactly sign
extension. -/
theorem unsigned_agrees :
    eval_op .Op_GetMask 32 [mk_bv 32 1, mk_bv 32 4294967295] = mk_bv 32 1 := by
  decide +kernel

/-- The existing `Op_Sext` reproduces the native widening from the certificate's
own operators: sign-extending the width-1 `-1` to 32 bits gives the all-ones
operand, and the mask then comes out full. -/
theorem sext_reproduces_native :
    eval_op .Op_GetMask 32
      [eval_op .Op_Sext 32 [mk_bv 1 1, mk_bv 32 1], mk_bv 32 4294967295]
      = mk_bv 32 4294967295 := by
  decide +kernel

/-- …while sign-extending an unsigned-valued operand changes nothing, so the
candidate adaptation is inert exactly where the native path says it is. -/
theorem sext_inert_on_zero :
    eval_op .Op_GetMask 32
      [eval_op .Op_Sext 32 [mk_bv 1 0, mk_bv 32 1], mk_bv 32 4294967295]
      = mk_bv 32 0 := by
  decide +kernel

#print axioms cert_narrow
#print axioms native_signed
#print axioms unsigned_agrees
#print axioms sext_reproduces_native
#print axioms sext_inert_on_zero

end Audit.PicoMaskSigned
