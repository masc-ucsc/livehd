/*verilator lint_off UNOPTFLAT*/
/*verilator lint_off UNUSED*/
module han_carl
(
  input             clk,
  input             reset,

  input [15:0]      a,
  input [15:0]      b,

  output [15:0]     s

);

reg c;
wire [15:0] h,g,p;
wire [15:0] e,f,w,x,y,z,m,n,o,t;
genvar i;


  assign c = 1'b0;
  for(i=0; i<16; i=i+1) begin
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

//  buff bu1(.A(g[0]),.B(p[0]),.C(e[0]),.D(f[0]));
   for(i=0; i<15; i=i+2) begin
     buff bu1
     (
       .A(g[i]),
       .B(p[i]),
       .C(e[i]),
       .D(f[i])
     );
   end

   for(i=1; i<16; i=i+2) begin
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

  for(i=2; i<15; i=i+2) begin
     buff bu4
     (
       .A(e[i]),
       .B(f[i]),
       .C(w[i]),
       .D(x[i])
     );
  end

  for(i=3; i<16; i=i+2) begin
     pref_op pref
     (
       .gi(e[i]),
       .pi(f[i]),
       .gk(e[i-2]),
       .pk(f[i-2]),
       .go(w[i]),
       .po(x[i])
     );
  end

//4th BLOCK

  buff bu5(.A(w[0]),.B(x[0]),.C(y[0]),.D(z[0]));
  buff bu6(.A(w[1]),.B(x[1]),.C(y[1]),.D(z[1]));
  buff bu7(.A(w[2]),.B(x[2]),.C(y[2]),.D(z[2]));
  buff bu8(.A(w[3]),.B(x[3]),.C(y[3]),.D(z[3]));

   for(i=4; i<15; i=i+2) begin
     buff bu9
     (
       .A(w[i]),
       .B(x[i]),
       .C(y[i]),
       .D(z[i])
     );
   end


  for(i=5; i<16; i=i+2) begin
     pref_op pref
     (
       .gi(w[i]),
       .pi(x[i]),
       .gk(w[i-4]),
       .pk(x[i-4]),
       .go(y[i]),
       .po(z[i])
     );
  end

//5th BLOCK

 buff bu10(.A(y[0]),.B(z[0]),.C(m[0]),.D(n[0]));
 buff bu11(.A(y[1]),.B(z[1]),.C(m[1]),.D(n[1]));
 buff bu12(.A(y[2]),.B(z[2]),.C(m[2]),.D(n[2]));
 buff bu13(.A(y[3]),.B(z[3]),.C(m[3]),.D(n[3]));
 buff bu14(.A(y[4]),.B(z[4]),.C(m[4]),.D(n[4]));
 buff bu15(.A(y[5]),.B(z[5]),.C(m[5]),.D(n[5]));
 buff bu16(.A(y[6]),.B(z[6]),.C(m[6]),.D(n[6]));
 buff bu17(.A(y[7]),.B(z[7]),.C(m[7]),.D(n[7]));

 for(i=8; i<15; i=i+2) begin
     buff bu18
     (
       .A(y[i]),
       .B(z[i]),
       .C(m[i]),
       .D(n[i])
     );
 end


 for(i=9; i<16; i=i+2) begin
     pref_op pref
     (
       .gi(y[i]),
       .pi(z[i]),
       .gk(y[i-8]),
       .pk(z[i-8]),
       .go(m[i]),
       .po(n[i])
     );
 end


//6th BLOCK
 buff bu19(.A(m[0]),.B(n[0]),.C(o[0]),.D(t[0]));

 for(i=1; i<16; i=i+2) begin
     buff bu20
     (
       .A(m[i]),
       .B(n[i]),
       .C(o[i]),
       .D(t[i])
     );
 end

 for(i=2; i<15; i=i+2) begin
     pref_op pref
     (
       .gi(m[i]),
       .pi(n[i]),
       .gk(m[i-1]),
       .pk(n[i-1]),
       .go(o[i]),
       .po(t[i])
     );
 end

//7th BLOCK

 diamond diam (.Hi(h[0]),.Ci(c),.Si(s[0]));

 for(i=1; i<16; i=i+1) begin
     diamond diam
     (
       .Hi(h[i]),
       .Ci(o[i-1]),
       .Si(s[i])
     );
 end

endmodule

/*verilator lint_on UNOPTFLAT*/
/*verilator lint_on UNUSED*/

