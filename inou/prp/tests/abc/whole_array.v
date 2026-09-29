/*
:top: whole_array
:synth_set: pass.abc.memory=true
:lec_set: formal.bound=2
*/
module whole_array(input clock, reset, load, we, input [1:0] index,
                   input [15:0] bulk, init_data, input [3:0] value,
                   output [15:0] allq, output [3:0] rd);
  logic [3:0] q [4], new_q [4], reset_q [4];
  for (genvar k=0; k<4; k=k+1) begin
    assign new_q[k] = bulk[k*4 +: 4];
    assign reset_q[k] = init_data[k*4 +: 4];
  end
  always_ff @(posedge clock) begin
    if (reset) q <= reset_q;
    else begin
      if (load) q <= new_q;
      if (we) q[index] <= value;
    end
  end
  assign allq = {q[3],q[2],q[1],q[0]};
  assign rd = q[index];
endmodule
