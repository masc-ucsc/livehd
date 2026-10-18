// Exhaustive casez/casex statements with `?` / `x` labels: read_slang spells the don't-care bits `-`, which
// still cover the whole selector (see full_case_hold.sv). Not compared with lec: the native reader models an
// exhaustive wildcard case without a default as a latch whose enable it cannot fold to one.
module full_case_dontcare (
    input  [3:0] op,
    input        a,
    input        b,
    output reg   y_z,
    output reg   y_x,
    output reg [3:0] y_u
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

  // `unique casez` over {unsigned, sign, tag} that lists all sixteen values and still ends in an empty default
  // (bp_be_int_unbox). The front end already marks it full_case, but the dead default arm kept the hold.
  always_comb
    unique casez ({op[3], op[2], op[1:0]})
      {2'b1?, 2'd3}: y_u = {a, b, a, b};
      {2'b1?, 2'd2}: y_u = {b, b, a, a};
      {2'b1?, 2'd1}: y_u = {a, a, b, b};
      {2'b1?, 2'd0}: y_u = {a, b, b, a};
      {2'b01, 2'd3}: y_u = 4'd1;
      {2'b01, 2'd2}: y_u = 4'd2;
      {2'b01, 2'd1}: y_u = 4'd4;
      {2'b01, 2'd0}: y_u = 4'd8;
      {2'b00, 2'd3}: y_u = 4'd3;
      {2'b00, 2'd2}: y_u = 4'd5;
      {2'b00, 2'd1}: y_u = 4'd9;
      {2'b00, 2'd0}: y_u = 4'd6;
      default: begin end
    endcase
endmodule
