// Golden for reg_nested_named_tuple_reset: one register per leaf field, named
// like the Pyrope field, each restored to its initializer by the reset.
module reg_nested_named_tuple_reset (
  input        clock,
  input        reset,
  input        we,
  input  [3:0] v,
  output [3:0] sa,
  output [3:0] sx,
  output [1:0] sy,
  output [3:0] pa,
  output [3:0] px,
  output [1:0] py,
  output [3:0] ua,
  output [3:0] ux,
  output [1:0] uy,
  output [2:0] dk,
  output [3:0] da,
  output [3:0] dx,
  output [1:0] dy,
  output [3:0] cx,
  output [1:0] cy,
  output [3:0] na,
  output [3:0] nx,
  output [1:0] ny,
  output [3:0] fa,
  output [3:0] fx,
  output [1:0] fy
);
  reg [3:0] \st.a , \st.n.x ;
  reg [1:0] \st.n.y ;
  reg [3:0] \sp.a , \sp.n.x ;
  reg [1:0] \sp.n.y ;
  reg [3:0] \su.a , \su.n.x ;
  reg [1:0] \su.n.y ;
  reg [2:0] \sd.k ;
  reg [3:0] \sd.t.a , \sd.t.n.x ;
  reg [1:0] \sd.t.n.y ;
  reg [3:0] \sc.a , \sc.n.x ;
  reg [1:0] \sc.n.y ;
  reg [3:0] \sn.a , \sn.n.x ;
  reg [1:0] \sn.n.y ;
  reg [3:0] \sf.a , \sf.n.x ;
  reg [1:0] \sf.n.y ;

  always @(posedge clock) begin
    if (reset) begin
      \st.a     <= 4'd1;
      \st.n.x   <= 4'd5;
      \st.n.y   <= 2'd2;
      \sp.a     <= 4'd6;
      \sp.n.x   <= 4'd3;
      \sp.n.y   <= 2'd1;
      \su.a     <= 4'd2;
      \su.n.x   <= 4'd9;
      \su.n.y   <= 2'd3;
      \sd.k     <= 3'd4;
      \sd.t.a   <= 4'd7;
      \sd.t.n.x <= 4'd10;
      \sd.t.n.y <= 2'd1;
      \sc.a     <= 4'd0;
      \sc.n.x   <= 4'd11;
      \sc.n.y   <= 2'd3;
      \sn.a     <= 4'd3;
      \sn.n.x   <= 4'd12;
      \sn.n.y   <= 2'd1;
      \sf.a     <= 4'd5;
      \sf.n.x   <= 4'd8;
      \sf.n.y   <= 2'd2;
    end else if (we) begin
      \st.n.x   <= v;
      \st.n.y   <= v[1:0];
      \sp.a     <= v;
      \sp.n.y   <= v[3:2];
      \su.n.x   <= v;
      \sd.k     <= v[2:0];
      \sd.t.n.x <= v;
      \sc.n.y   <= v[2:1];
      \sn.n.y   <= v[3:2];
      \sf.n.x   <= v;
    end
  end

  assign sa = \st.a ;
  assign sx = \st.n.x ;
  assign sy = \st.n.y ;
  assign pa = \sp.a ;
  assign px = \sp.n.x ;
  assign py = \sp.n.y ;
  assign ua = \su.a ;
  assign ux = \su.n.x ;
  assign uy = \su.n.y ;
  assign dk = \sd.k ;
  assign da = \sd.t.a ;
  assign dx = \sd.t.n.x ;
  assign dy = \sd.t.n.y ;
  assign cx = \sc.n.x ;
  assign cy = \sc.n.y ;
  assign na = \sn.a ;
  assign nx = \sn.n.x ;
  assign ny = \sn.n.y ;
  assign fa = \sf.a ;
  assign fx = \sf.n.x ;
  assign fy = \sf.n.y ;
endmodule
