// A function called first inside an `if` branch and then at module scope: each
// call binds its own formals (the shared `a0` used to keep the first call's
// branch-scoped declaration, and a later call inferred its range from `fb[4]`).
module function_formal_per_call(input clock, input reset, input [1:0] s, input [4:0] fb, input [7:0] w, output reg [3:0] o0, output [3:0] o1, output [3:0] o2);
  function automatic [3:0] f0(input [7:0] a0);
    f0 = a0[3:0];
  endfunction
  always @(*) begin
    o0 = 0;
    if (s == 1) o0 = f0(w);
  end
  assign o1 = f0(fb[4]);
  assign o2 = f0(w);
endmodule
