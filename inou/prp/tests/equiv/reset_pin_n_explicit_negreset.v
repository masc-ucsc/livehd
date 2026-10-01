// Golden: `clr_n` resets `r` while HIGH (explicit negreset=false), `clr`
// resets `s` while LOW (explicit negreset=true). Both resets are synchronous.
module reset_pin_n_explicit_negreset(input clk, input clr_n, input clr, input [3:0] d,
                                     output [3:0] q, output [3:0] p);
  reg [3:0] r;
  reg [3:0] s;
  always @(posedge clk) begin
    if (clr_n)          r <= 4'd5;
    else if (d != 4'd0) r <= d;
    if (!clr)           s <= 4'd9;
    else if (d != 4'd0) s <= d;
  end
  assign q = r;
  assign p = s;
endmodule
