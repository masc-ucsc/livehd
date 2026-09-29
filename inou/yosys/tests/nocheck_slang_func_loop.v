// A constant-bound `for` loop inside a `function automatic`, reached from a
// CONTINUOUS ASSIGN and from a NET INITIALIZER -- neither of which goes
// through lower_process, where the slang unroll budget used to be armed. With
// a budget of 0 the very first `unroll_tick` failed, so even `for (int i = 0;
// i < 1; i++)` died with "loop unroll limit of 4000 exhausted" (bedrock-rtl's
// br_lfsr spells its state advance exactly this way). `y_proc` is the control:
// the same loop inside always_comb always worked.
//
// nocheck_ keeps the file out of the plain-yosys glob (SV int declarations).
module slang_func_loop(input [7:0] a, output [7:0] y_assign, output [7:0] y_netinit, output [7:0] y_proc);
  localparam integer Steps = 3;

  function automatic [7:0] advance1(input [7:0] s);
    reg [7:0] acc;
    begin
      acc = s;
      for (int i = 0; i < 1; i = i + 1) begin
        acc = {acc[6:0], ^(acc & 8'hb4)};
      end
      advance1 = acc;
    end
  endfunction

  function automatic [7:0] advanceN(input [7:0] s);
    reg [7:0] acc;
    begin
      acc = s;
      for (int i = 0; i < Steps; i = i + 1) begin
        acc = {acc[6:0], ^(acc & 8'hb4)};
      end
      advanceN = acc;
    end
  endfunction

  // 1. continuous assign
  assign y_assign = advance1(a);

  // 2. net initializer
  wire [7:0] w = advanceN(a);
  assign y_netinit = w;

  // 3. control: the same unroll inside a process
  reg [7:0] acc_p;
  always @(*) begin
    acc_p = a;
    for (int i = 0; i < Steps; i = i + 1) begin
      acc_p = {acc_p[6:0], ^(acc_p & 8'hb4)};
    end
  end
  assign y_proc = acc_p;
endmodule
