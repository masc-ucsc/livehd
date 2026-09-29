module reference(input clock, reset, load, we, input [1:0] index,
                 input [15:0] bulk, init_data, input [3:0] value,
                 output reg [15:0] allq, output [3:0] rd);
  reg [15:0] next_q;
  always_comb begin
    next_q = load ? bulk : allq;
    if (we) next_q[index*4 +: 4] = value;
    if (reset) next_q = init_data;
  end
  always @(posedge clock) allq <= next_q;
  assign rd = allq[index*4 +: 4];
endmodule
