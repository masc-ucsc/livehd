// Golden for reg_tuple_field_bit_write: nonblocking bit writes to one register
// in one cycle all land (a later one wins where they overlap). `st` and `w`
// reset to 0; `nt` has no reset. The registers carry the Pyrope field names.
module reg_tuple_field_bit_write (
  input        clock,
  input        reset,
  input  [3:0] v,
  input  [3:0] d,
  input  [1:0] i,
  input  [1:0] e,
  output [1:0] q,
  output [1:0] p,
  output [3:0] c,
  output [3:0] x,
  output [1:0] y,
  output [3:0] z
);
  reg  [1:0] \st.a , \st.b , w;
  reg  [3:0] \nt.c , \nt.n.x ;
  reg  [1:0] \nt.n.y ;
  wire [1:0] e1 = e + 2'd1;

  always @(posedge clock) begin
    if (reset) begin
      \st.a  <= 2'd0;
      \st.b  <= 2'd0;
      w      <= 2'd0;
    end else begin
      if (v[0]) begin
        \st.a [0] <= d[0];
        w[0]      <= d[0];
      end
      if (v[1]) begin
        \st.a [1] <= d[1];
        w[1]      <= d[1];
      end
    end
    if (v[0]) \nt.c [0] <= d[0];
    if (v[1]) \nt.c [i] <= d[3];
    if (v[2]) \nt.n.x [2:1] <= e1;
    \nt.n.x [3] <= d[1];
    \nt.n.y [1] <= e[0];
    if (v[3]) \nt.n.x [0] <= d[2];
  end

  assign q = \st.a ;
  assign p = w;
  assign c = \nt.c ;
  assign x = \nt.n.x ;
  assign y = \nt.n.y ;
  assign z = \nt.c ;
endmodule
