/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
module kogg_stone_8
(
  input             clk,
  input             reset,

  input [7:0]      a,
  input [7:0]      b,

  output [7:0]     s

);

reg c;
wire [7:0] h,g,p;
wire [7:0] e,f,w,x,y,z;
genvar i;


  assign c = 1'b0;
  for(i=0; i<8; i=i+1) begin
   square_op sq
   (
     .Ai(a[i]),
     .Bi(b[i]),
     .Hi(h[i]),
     .Pi(p[i]),
     .Gi(g[i])
   );
  end

//2nd BLOCK

  buff bu1(.A(g[0]),.B(p[0]),.C(e[0]),.D(f[0]));

   for(i=1; i<8; i=i+1) begin
     pref_op pref
     (
       .gi(g[i]),
       .pi(p[i]),
       .gk(g[i-1]),
       .pk(p[i-1]),
       .go(e[i]),
       .po(f[i])
     );
   end

//3rd BLOCK

  buff bu2(.A(e[0]),.B(f[0]),.C(w[0]),.D(x[0]));
  buff bu3(.A(e[1]),.B(f[1]),.C(w[1]),.D(x[1]));

  for(i=2; i<8; i=i+1) begin
     pref_op pref
     (
       .gi(e[i]),
       .pi(f[i]),
       .gk(e[i-1]),
       .pk(f[i-1]),
       .go(w[i]),
       .po(x[i])
     );
  end

//4th BLOCK

  buff bu4(.A(w[0]),.B(x[0]),.C(y[0]),.D(z[0]));
  buff bu5(.A(w[1]),.B(x[1]),.C(y[1]),.D(z[1]));
  buff bu6(.A(w[2]),.B(x[2]),.C(y[2]),.D(z[2]));
  buff bu7(.A(w[3]),.B(x[3]),.C(y[3]),.D(z[3]));

  for(i=4; i<8; i=i+1) begin
     pref_op pref
     (
       .gi(w[i]),
       .pi(x[i]),
       .gk(w[i-1]),
       .pk(x[i-1]),
       .go(y[i]),
       .po(z[i])
     );
  end

//5th BLOCK

 diamond diam (.Hi(h[0]),.Ci(c),.Si(s[0]));

 for(i=1; i<8; i=i+1) begin
     diamond diam
     (
       .Hi(h[i]),
       .Ci(y[i-1]),
       .Si(s[i])
     );
 end

endmodule

/*verilator lint_on UNOPTFLAT*/
/*verilator lint_on UNUSED*/

