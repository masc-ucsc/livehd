// TRACKER — a BLOCKING array write inside an edge process is not modelled by
// `--reader slang` (lhdsuite array_problem.md, 2026-07-27).
//
// Verilog mandates: `mem` is PERSISTENT state, and because the write is
// blocking it executes before the read in the same block, so a same-cycle
// same-address collision must deliver the NEW data (yosys agrees — it demotes
// the memory to per-entry registers via mem2reg and lgcheck PROVES the
// `--reader yosys-verilog` round trip).
//
// `--reader slang` used to lower this SILENTLY as a stateless `mut` array
// (Slang_context::collect_state_vars admits only NONBLOCKING-written symbols
// to reg_syms_), i.e. `q = (we && wa==ra) ? wd : 0` — the forwarding half
// accidentally right, the STORAGE destroyed. lgcheck REFUTED it against this
// file. It now emits the `blocking-ff-array` unsupported diagnostic instead,
// so the miscompile is fail-stop rather than silent.
//
// TO FIX: model a blocking-written array as state, the way yosys does —
// demote it to per-entry registers, or infer a memory whose read port carries
// the matching transparency bit. Then this file should COMPILE and its
// prp-v2prp2v-* round trip should PROVE.
module mem_blocking_write(input logic clk, input logic we,
                          input logic [1:0] wa, input logic [1:0] wd,
                          input logic [1:0] ra, output logic [1:0] q);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[wa] = wd;   // BLOCKING: visible to the read below
    q <= mem[ra];
  end
endmodule
