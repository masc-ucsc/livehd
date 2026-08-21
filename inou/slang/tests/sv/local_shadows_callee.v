// A local signal that carries the SAME name as a module the unit instantiates.
// Legal Verilog (module and net namespaces are distinct); in the emitted
// Pyrope the file-scope `const leaf = import("leaf.leaf")` and the body's
// `mut leaf` are one namespace, so the import alias must step aside.
// Reduced from CVA6's instr_queue (`logic popcount` + `popcount #(...) i_popcount`).
module leaf (
    input  logic a,
    output logic y
);
  assign y = ~a;
endmodule

module local_shadows_callee (
    input  logic a,
    input  logic b,
    output logic y,
    output logic z
);
  logic leaf;
  assign leaf = a & b;

  leaf i_leaf (
      .a(a),
      .y(y)
  );

  assign z = leaf;
endmodule
