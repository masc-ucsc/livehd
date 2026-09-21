/*
*/
module bad_assign(input rst, input pulse, output [31:0] tick_count);
  reg [31:0] ms_counter;
  always @(posedge pulse or posedge rst) begin
    if (rst) ms_counter = 0;
    else     ms_counter = ms_counter + 1;
  end
  assign tick_count = ms_counter;
endmodule
