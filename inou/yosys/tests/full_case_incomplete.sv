// An inner case that does NOT list every selector value really holds its result when no label matches. In this
// nested shape read_slang leaves the hold as a path through muxes (not a latch), so it is a combinational loop
// that pass.legalize reports. yosys_full_case_test.sh checks that lhd_full_case leaves such a case alone.
module full_case_incomplete (
    input  [1:0] mode,
    input  [1:0] rs,
    input        a,
    input        b,
    output reg   y
);
  always @(*) begin
    case (mode)
      2'd0:
      case (rs)
        2'b00, 2'b01: y = 1'b0;
        2'b10:        y = a;
      endcase
      default: y = b;
    endcase
  end
endmodule
