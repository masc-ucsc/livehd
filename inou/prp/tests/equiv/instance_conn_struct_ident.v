// Golden for instance_conn_struct_ident: the slang reader's per-module
// struct-classification collector sets (deep-accessed / whole-copied /
// deep-written / field-read / pattern-assigned / array-bundle) are rebuilt for
// every module, and lowering a submodule INSTANCE mid-emission recurses into
// lower_module for the callee — which rebuilt those sets for the child and
// never restored the parent's. After the first instance, is_scalar_struct_var
// answered with the CHILD's collector state, flipping a struct net's identity
// between its store and its later reads: the '{...}' whole-net assign stored
// the FLAT bus (deep-accessed + array-of-struct field forces flat), while
// every read lowered after the instance resolved through never-written bundle
// leaves (XiangShan Alu_3: io_out_valid = io_in.valid stuck at 0). The
// trigger needs all three: (1) a struct net with an array-of-struct field,
// (2) a whole-net '{...}' assignment, (3) a submodule instance (its callee
// not yet lowered) whose input connection deep-reads that net. Functionally:
// out_valid = in_valid and out_r = ctrl.rfWen = in_bits[10].
module icsi_sub(
  input  [7:0] a,
  output [7:0] r
);
  assign r = a;
endmodule

// NOTE: the top is deliberately UNDOTTED (not \instance_conn_struct_ident.top) so lgcheck's
// slang gold reader can name it via --top (dotted escaped names break
// yosys-slang's --top/RTLIL naming) — same convention as
// instance_out_struct_ident_top.
module instance_conn_struct_ident_top (
  input         in_valid,
  input  [19:0] in_bits,
  output        out_valid,
  output [7:0]  out_r
);
  wire struct packed {
    logic ready;
    logic valid;
    struct packed {
      struct packed {logic [8:0] fuOpType; logic rfWen; } ctrl;
      struct packed {logic [8:0] fuOpType; logic rfWen; }[0:0] ctrlPipe;
    } bits;
  } io_in;
  wire out_valid_0 = io_in.valid;
  assign io_in = '{ready: (1'h1), valid: in_valid, bits: in_bits};
  icsi_sub u_sub(.a({7'h0, io_in.bits.ctrl.rfWen}), .r(out_r));
  assign out_valid = out_valid_0;
endmodule
