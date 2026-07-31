// Golden for func_ret_reused_in_loop — a `function automatic` called once per
// unrolled loop iteration, accumulated into one output.
//
// SystemVerilog returns through a variable named after the FUNCTION, so N
// inlined calls all write the single name `f`. Each write is immediately
// consumed by the accumulator, so the LNAST is correct and read-after-write in
// program order. The Pyrope WRITER then folded the accumulator chain: every
// `%t = acc | f` was adjacent to its use (foldable), and the `o__wN` copies
// folded on top of that -- which slid all N reads of `f` down past all N
// writes, emitting `o = (0 | f) | f` where both reads see the LAST iteration.
//
// A silent miscompile, and not a toy one: minion's `vpu_trans` is exactly this
// shape (`id_trans_busy_o |= is_used(states_q[i])` over 7 pipeline slots) and
// came out as seven reads of slot 6, which //bench:minion_lec caught as
// `id_trans_busy_o(ref=1 impl=0)`.
//
// The fix is operand stability THROUGH the inlines the writer performs
// (operands_stable_deep): a fold may only move a cone across statements that
// write nothing the cone reads. The interesting direction here is therefore the
// round trip -- prp-v2prp2v-* -- which reads this golden and re-emits
// it as Pyrope.
module func_ret_reused_in_loop (
  input  logic [3:0] a_i,
  output logic       o
);
  function automatic logic f(input logic [1:0] s);
    begin
      case (s)
        2'b00, 2'b11: f = 1'b0;
        default:      f = 1'b1;
      endcase
    end
  endfunction

  always_comb begin
    o = 1'b0;
    for (int i = 0; i < 2; ++i) begin
      o |= f(a_i[2*i +: 2]);
    end
  end
endmodule
