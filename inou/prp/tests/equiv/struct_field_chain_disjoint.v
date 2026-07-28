// Golden for struct_field_chain_disjoint — the per-field WRITE CHAIN false
// combinational loop, DISJOINT-lane leg.
//
// A NESTED packed-struct VARIABLE is written one field at a time inside a
// single always_comb, and a later field's value depends on a read of an
// EARLIER field of the same variable. Per field the dataflow is a DAG:
//
//     s_i -> c.s.a -> c.rm -> p -> c.imm
//
// nothing feeds itself, and Verilator --lint-only -Wall reports no UNOPTFLAT.
// But a nested struct variable cannot be split into per-leaf nets (that path
// is one level deep), so it lowers to ONE packed bus and the three field
// writes become a Set_mask chain on that single net. The `c.rm` read at the
// bottom then binds to the LAST version -- the one produced by the `c.imm`
// write -- and since `c.imm` depends on `p`, which depends on that very read,
// the WORD-level graph closes a loop the bit-level design does not have.
//
// The fix is in pass/cprop scalar_get_mask_packed: a Set_mask whose lane is
// provably DISJOINT from the slice being read cannot affect it, so the read
// walks past it to the base. Here that means skipping the `.imm` write and
// landing on the `.rm` write, which breaks the cycle.
//
// This pair is the DISJOINT leg on its own: it is dissolved by the skip alone.
// struct_field_chain_own_lane is the complementary CONTAINED leg, which the
// skip cannot fix.
//
// Semantics: rm = s_i; imm = (s_i == 1) ? 0 : i_i; o = {rm, imm, s_i}.
//
// NOTE: the top is deliberately UNDOTTED so lgcheck's slang gold reader can
// name it via --top (dotted escaped names break yosys-slang's --top/RTLIL
// naming) — same convention as struct_selfref_pattern_top.
typedef struct packed {
  logic [1:0] a;
} sfcd_inner_t;

typedef struct packed {
  logic [1:0]  rm;
  logic [1:0]  imm;
  sfcd_inner_t s;
} sfcd_ctrl_t;

module struct_field_chain_disjoint_top (
  input  logic [1:0] i_i,
  input  logic [1:0] s_i,
  output logic [5:0] o
);
  sfcd_ctrl_t c;
  logic       p;

  always_comb begin
    c.s.a = s_i;                 // W0: writes lane .s
    c.rm  = c.s.a;               // W1: writes lane .rm, reads lane .s
    c.imm = p ? 2'b0 : i_i;      // W2: writes lane .imm, reads p
  end

  // reads lane .rm -- binds to W2's version, which is DISJOINT from .rm
  assign p = (c.rm == 2'b01);

  assign o = c;
endmodule
