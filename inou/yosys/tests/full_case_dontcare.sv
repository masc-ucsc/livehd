// Exhaustive casez/casex statements with `?` / `x` labels: read_slang spells the don't-care bits `-`, which
// still cover the whole selector (see full_case_hold.sv). Not compared with lec: the native reader models an
// exhaustive wildcard case without a default as a latch whose enable it cannot fold to one.
module full_case_dontcare (
    input  [3:0] op,
    input        a,
    input        b,
    output reg   y_z,
    output reg   y_x
);
  always @(*) begin
    casez (op[1:0])
      2'b0?: y_z = a;
      2'b1?: y_z = b;
    endcase
  end

  always @(*) begin
    casex (op[2:1])
      2'b0x: y_x = a;
      2'b1x: y_x = b;
    endcase
  end
endmodule
