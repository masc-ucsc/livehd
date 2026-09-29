module stage(input clock, input reset, input [7:0] d, output [7:0] q);
  reg [7:0] reg_mem_writedata;
  always @(posedge clock) if (reset) reg_mem_writedata <= 8'd0;
                          else        reg_mem_writedata <= d;
  assign q = reg_mem_writedata;
endmodule
module csdut(input clock, input reset, input [7:0] da, input [7:0] db,
             output [7:0] qa, output [7:0] qb);
  stage pipeA_ex_mem(.clock(clock), .reset(reset), .d(da), .q(qa));
  stage pipeB_ex_mem(.clock(clock), .reset(reset), .d(db), .q(qb));
endmodule
