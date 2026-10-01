// Golden for spec_memory_clock_pin: `ma` on ca, `mb` on cb.
module spec_memory_clock_pin(input ca, input cb, input we, input [1:0] a, input [7:0] d,
                             input [1:0] ra, output [7:0] qa, output [7:0] qb);
  reg [7:0] ma [0:3];
  reg [7:0] mb [0:3];
  always @(posedge ca) if (we) ma[a] <= d;
  always @(posedge cb) if (we) mb[a] <= d;
  assign qa = ma[ra];
  assign qb = mb[ra];
endmodule
