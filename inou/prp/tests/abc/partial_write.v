/*
:top: partial_write
:synth_set: pass.abc.memory=true
:lec_set: formal.bound=2
*/
module partial_write(input clock, reset, load, we, input [1:0] index,
                     input [15:0] bulk, init_data, input [3:0] value,
                     output reg [15:0] allq, output [3:0] rd);
  always @(posedge clock) begin
    if (reset) allq <= init_data;
    else begin
      if (load) allq <= bulk;
      if (we) allq[index*4 +: 4] <= value;
    end
  end
  assign rd = allq[index*4 +: 4];
endmodule
