// Golden for reg_rw_rolled_loop, written per bit with no loop. Every read in
// the Pyrope loops is a read of the register's Q (nonblocking semantics), and a
// bit whose enable is low holds.
module reg_rw_rolled_loop (
  input        clock,
  input        reset,
  input  [1:0] v,
  input  [1:0] d,
  input  [3:0] e,
  input        s,
  output [1:0] q,
  output [3:0] w,
  output [1:0] p,
  output [1:0] y,
  output [3:0] m,
  output [1:0] k,
  output       x,
  output [3:0] z
);
  reg [1:0] r;
  reg [3:0] t;
  reg [1:0] a0;
  reg [1:0] a1;
  reg [3:0] n;
  reg [1:0] b;
  reg [3:0] f;

  always @(posedge clock) begin
    if (reset) begin
      r  <= 2'd0;
      t  <= 4'd0;
      a0 <= 2'd0;
      a1 <= 2'd0;
      n  <= 4'd0;
      b  <= 2'd0;
      f  <= 4'd0;
    end else begin
      if (s) f <= {2'b0, d} ^ 4'd1;  // the last iteration's write
      if (v[0]) begin
        r[0]  <= d[0];
        a0[0] <= a1[0];
        a1[0] <= d[0];
        b[0]  <= d[0];
      end
      if (v[1]) begin
        r[1]  <= d[1];
        a0[1] <= a1[1];
        a1[1] <= d[1];
        b[1]  <= d[1];
      end
      if (e[0]) begin
        t[0] <= t[3] ^ s;
        n[0] <= d[0] ^ n[3];
      end
      if (e[1]) begin
        t[1] <= t[3] ^ s;
        n[1] <= d[1] ^ n[2];
      end
      if (e[2]) begin
        t[2] <= t[3] ^ s;
        n[2] <= d[0] ^ n[1];
      end
      if (e[3]) begin
        t[3] <= t[3] ^ s;
        n[3] <= d[1] ^ n[0];
      end
    end
  end

  assign q = r;
  assign w = t;
  assign p = a0;
  assign y = a1;
  assign m = n;
  assign k = b;
  assign x = s & (v[0] ^ v[1]);
  assign z = f;
endmodule
