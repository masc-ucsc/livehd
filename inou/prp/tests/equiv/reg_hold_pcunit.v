module pcunit(input taken, input stall, input [7:0] tgt, input [7:0] nx,
              output [7:0] pc, input clock, input reset);
  reg [7:0] pcr;
  always @(posedge clock)
    if (reset)       pcr <= 8'b0;
    else if (taken)  pcr <= tgt;
    else if (stall)  pcr <= pcr;
    else             pcr <= nx;
  assign pc = pcr;
endmodule
