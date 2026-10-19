// Exhaustive case statements in a combinational block that does not otherwise assign its result: the shape of
// Vortex VX_fp_rounding, VX_alu_int and VX_fncp_unit. read_slang --no-proc starts such a block from "hold the
// previous value" and proc_mux wires that hold into the unmatched path of every case. When the labels already
// list every selector value that path is dead, and it must not survive as a combinational loop through a mux.
module full_case_hold (
    input  [1:0] mode,
    input  [1:0] rs,
    input  [3:0] op,
    input        sel1,
    input        a,
    input        b,
    input        c,
    output reg   y_nested,
    output reg [3:0] y_wide,
    output reg   y_default
);
  // An inner case that lists all four values, with no default, under an outer case that has one.
  always @(*) begin
    case (mode)
      2'd0:
      case (rs)
        2'b00, 2'b01: y_nested = 1'b0;
        2'b10:        y_nested = a;
        2'b11:        y_nested = 1'b1;
      endcase
      2'd1:    y_nested = b;
      default: y_nested = c;
    endcase
  end

  // Unsized integer labels are 32 bits wide, so the selector is {29'b0, op[2:0]}: only its variable bits count.
  always @(*) begin
    case (op[2:0])
      0, 1, 2: y_wide = {a, b, c, 1'b0};
      3:       y_wide = 4'd5;
      4, 5:    y_wide = {4{a}};
      6, 7:    y_wide = {b, b, a, a};
    endcase
  end

  // A one-bit selector with both values listed and an empty default that can never run.
  always @(*) begin
    case (sel1)
      1'b0: y_default = a;
      1'b1: y_default = b;
      default: ;
    endcase
  end
endmodule
