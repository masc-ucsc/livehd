// Golden for struct_dyn_write_blocking: runtime-position BLOCKING writes into
// a packed-struct variable, which inou/slang stores as one leaf per field.
//   q:  `s = d`, then bit `b` of the whole struct, a `+:` window of member `g`
//       (IEEE 1800 11.5.1: only its in-range bits, never member `f`), element
//       `j` of the array member (j = 3..7 writes nothing), and a constant
//       select below that member's element 1.
//   qf: the same writes on a struct local of a function.
//   qu: a struct whose fields have two drivers; the runtime write into `g`
//       must not rewrite the field `f` the continuous assign drives.
// Checked against iverilog with a flattened copy (random differential, 0
// mismatches).
module struct_dyn_write_blocking (
  input  [4:0]  b,
  input         x,
  input  [2:0]  c,
  input  [3:0]  y,
  input  [2:0]  j,
  input  [3:0]  z,
  input  [31:0] d,
  output [31:0] q,
  output [31:0] qf,
  output [31:0] qu
);
  typedef struct packed {
    logic [7:0]      f;
    logic [7:0]      g;
    logic [3:0]      h;
    logic [2:0][3:0] arr;
  } s_t;

  s_t s;
  always_comb begin
    s = d;
    s[b] = x;
    s.g[c +: 4] = y;
    s.arr[j] = z;
    s.arr[1][1:0] = z[1:0];
  end
  assign q = s;

  function automatic logic [31:0] fs(input logic [31:0] dd, input logic [4:0] bb, input logic [2:0] cc, input logic [3:0] yy);
    s_t t;
    t = dd;
    t[bb] = 1'b1;
    t.g[cc +: 4] = yy;
    return t;
  endfunction
  assign qf = fs(d, b, c, y);

  s_t u;
  assign u.f = d[31:24];
  always @(*) begin
    u.g = d[23:16];
    u.g[c +: 4] = y;
    u.h = d[15:12];
    u.arr = d[11:0];
  end
  assign qu = u;
endmodule
