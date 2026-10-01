// Golden for spec_memory_initial_poweron_only: a plain memory, no reset.
module spec_memory_initial_poweron_only(input ck, input we, input [1:0] a, input [7:0] d,
                                        input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  always @(posedge ck) begin
    if (we) mem[a] <= d;
  end
  assign q = mem[ra];
endmodule
