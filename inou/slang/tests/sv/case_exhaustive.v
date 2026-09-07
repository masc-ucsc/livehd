module case_exhaustive(input logic [1:0] sel, input logic d,
                       output logic [3:0] table_o, output logic held_o);
  always_comb begin
    case (sel)
      0: table_o = 4'h3;
      1: table_o = 4'h5;
      2: table_o = 4'h9;
      3: table_o = 4'ha;
    endcase
  end
  // A duplicate arm does not complete the domain; sel=3 must hold state.
  always_latch begin
    case (sel)
      0: held_o <= d;
      1: held_o <= ~d;
      2: held_o <= 0;
      2: held_o <= 1;
    endcase
  end
endmodule
