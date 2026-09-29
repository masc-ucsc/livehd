module clocked_array_flags(input logic clk, reset, push, pop, addr, output wire [3:0] counts);
  reg [1:0] state[2];
  reg pushes[2], pops[2];
  integer k;
  always @(posedge clk) begin
    if (reset) begin
      for (k=0; k<2; k=k+1) state[k] <= 0;
    end else begin
      for (k=0; k<2; k=k+1) begin
        pushes[k] = 0;
        pops[k] = 0;
      end
      pushes[addr] = push;
      pops[addr] = pop;
      for (k=0; k<2; k=k+1) state[k] <= state[k] + pushes[k] - pops[k];
    end
  end
  assign counts = {state[1], state[0]};
endmodule
