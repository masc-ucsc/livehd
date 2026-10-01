// Golden for enum_mod_port.prp: `Color` (explicit values, widest 4) is a 3-bit
// port, `Dir` (one-hot, 4 entries: N=1, E=2, S=4, W=8) a 4-bit one, and
// `Hue` (one-hot, 3 entries: Blue=4) a 3-bit one.
// `Color.Green` (2) compares equal to itself, `Color.Blue` (4) does not, and
// reading the port back gives the entry value. `turn` rotates N->E->S->W->N
// (any other value of `d` also maps to N); `st` registers it, reset to S.
module enum_mod_port (
  input            clock,
  input            reset,
  input      [2:0] c,
  input      [3:0] d,
  input      [2:0] h,
  output           g0,
  output           g1,
  output     [7:0] v0,
  output           gc,
  output reg [3:0] t,
  output     [3:0] q,
  output           hb
);
  reg [3:0] st;
  assign q = st;

  assign g0 = 1'b1;
  assign g1 = 1'b0;
  assign v0 = 8'd4;
  assign gc = c == 3'd2;
  assign hb = h == 3'd4;

  always @(*) begin
    if (d == 4'd1) t = 4'd2;
    else if (d == 4'd2) t = 4'd4;
    else if (d == 4'd4) t = 4'd8;
    else t = 4'd1;
  end

  always @(posedge clock) begin
    if (reset) st <= 4'd4;
    else st <= t;
  end
endmodule
