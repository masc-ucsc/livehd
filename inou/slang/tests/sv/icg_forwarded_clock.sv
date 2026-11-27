// :test: sim
// The clock feeding the enable latch and AND comes back from the domain
// that consumes the gated clock. Its wire may still be open at elaboration.
module returned_clock_gate(input clock, enable, output gclk);
  reg held;
  always_latch if (!clock) held = enable;
  assign gclk = held & clock;
endmodule

module forwarded_domain(input root, gated, input [7:0] data,
                        output forwarded, output reg [7:0] captured);
  assign forwarded = root;
  always @(posedge gated) captured <= data;
endmodule

module icg_forwarded_clock(input clock, enable, input [7:0] data, output [7:0] captured);
  wire forwarded, gated;
  returned_clock_gate gate(forwarded, enable, gated);
  forwarded_domain domain(clock, gated, data, forwarded, captured);
endmodule
