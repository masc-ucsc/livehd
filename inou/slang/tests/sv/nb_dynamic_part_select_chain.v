// Two dynamic nonblocking part-select writes on one reg after a whole write of
// a COMPUTED value: the reader emits each as an in-place set_mask on the reg,
// and upass.ssa used to thread BOTH bases to the whole-write temp, so the
// first part-select was silently dropped (only the last survived). `lhd lec`
// reads both sides with slang, so the native check alone proves the
// miscompile; the lgyosys cross-check reads this source with yosys instead.
// The struct-typed reg is the same shape through a member element select.
// :lec_solver: lgyosys
module nb_dynamic_part_select_chain(input clk, input [11:0] a, b, input [1:0] i,
                                    input [3:0] j, input [5:0] v,
                                    output [11:0] o, output [11:0] p);
  typedef struct packed {
    logic [3:0]      hi;
    logic [1:0][3:0] arr;
  } s_t;
  reg [11:0] x;
  s_t        r;
  always @(posedge clk) begin
    x <= a ^ b;
    x[i*4 +: 4] <= v[3:0];
    x[j +: 2] <= v[5:4];
  end
  always @(posedge clk) begin
    r <= a & b;
    r.arr[i[0]] <= v[3:0];
    r[j -: 2] <= v[5:4];
  end
  assign o = x;
  assign p = r;
endmodule
