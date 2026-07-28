// Golden for struct_field_chain_own_lane — the per-field WRITE CHAIN false
// combinational loop, CONTAINED-lane leg.
//
// Same family as struct_field_chain_disjoint (a NESTED packed-struct VARIABLE
// written field by field in one always_comb, lowered to ONE packed bus and so
// to a Set_mask chain), but the write ORDER is the other way round: the field
// whose value depends on the read-back (`.typ`) is written BEFORE the field
// being read (`.rm`).
//
// Per field the dataflow is still a DAG -- s_i -> c.s.a -> c.rm -> p -> c.typ,
// nothing feeds itself, and Verilator --lint-only -Wall reports no UNOPTFLAT.
//
// This ordering is what makes the DISJOINT-lane skip insufficient. The `c.rm`
// read binds to the last version, which IS the `.rm` write, so there is no
// disjoint write to skip: the walk stops there. But that Set_mask's own `a`
// operand is the `.typ` write, which depends on `p`, which depends on this
// read -- the cycle survives.
//
// The fix is the second leg in pass/cprop scalar_get_mask_packed: a slice
// CONTAINED in the lane reads back exactly what `value` put there, so the read
// re-bases onto `value` and drops the dependency on `a` (and therefore on the
// whole earlier write chain) altogether.
//
// Semantics: rm = s_i; typ = (s_i == 1) ? 0 : i_i; o = {typ, rm, s_i}.
//
// NOTE: the top is deliberately UNDOTTED so lgcheck's slang gold reader can
// name it via --top (dotted escaped names break yosys-slang's --top/RTLIL
// naming) — same convention as struct_selfref_pattern_top.
typedef struct packed {
  logic [1:0] a;
} sfco_inner_t;

typedef struct packed {
  logic [1:0]  typ;
  logic [1:0]  rm;
  sfco_inner_t s;
} sfco_ctrl_t;

module struct_field_chain_own_lane_top (
  input  logic [1:0] i_i,
  input  logic [1:0] s_i,
  output logic [5:0] o
);
  sfco_ctrl_t c;
  logic       p;

  always_comb begin
    c.s.a = s_i;                 // W0: writes lane .s
    c.typ = p ? 2'b0 : i_i;      // W1: writes lane .typ, reads p
    c.rm  = c.s.a;               // W2: writes lane .rm, reads lane .s
  end

  // reads lane .rm -- binds to W2, whose own `a` operand is W1 (depends on p)
  assign p = (c.rm == 2'b01);

  assign o = c;
endmodule
