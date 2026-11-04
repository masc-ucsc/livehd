// Overlapping items of a plain (not unique/parallel) case: the FIRST matching
// item wins. The yosys-slang reader runs `proc -ifx`, and proc_mux in -ifx mode
// put all items in one parallel $pmux, so two matching items were both selected
// and pmuxtree took the LAST one (SoomRV FMul: underflow+inexact reported as
// inexact). packages/yosys.patch keeps the priority.
module casez_priority (
  input  logic [4:0] f,
  input  logic [3:0] g,
  input  logic [1:0] s,
  output logic [3:0] oz,
  output logic [3:0] ox,
  output logic [3:0] od
);
  // casez: f = 5'b00011 matches items 2 and 6; item 2 (9) wins
  always_comb begin
    casez (f)
      5'b00000: oz = 4'd0;
      5'b???1?: oz = 4'd9;
      5'b??1??: oz = 4'd10;
      5'b?1???: oz = 4'd11;
      5'b1????: oz = 4'd12;
      5'b????1: oz = 4'd8;
      default:  oz = 4'd15;
    endcase
  end
  // casex: g = 4'b1111 matches every item; the first (1) wins
  always_comb begin
    casex (g)
      4'b1xxx: ox = 4'd1;
      4'bx1xx: ox = 4'd2;
      4'bxx1x: ox = 4'd3;
      4'bxxx1: ox = 4'd4;
      default: ox = 4'd0;
    endcase
  end
  // a repeated label: the first item for s = 1 wins
  always_comb begin
    case (s)
      2'd0:    od = 4'd5;
      2'd1:    od = 4'd6;
      2'd1:    od = 4'd7;
      default: od = 4'd0;
    endcase
  end
endmodule
