// :test: roundtrip
// :lec_timeout: 30
package writer_enum_pkg;
  typedef enum logic [1:0] { User=0, Machine=3 } privilege_t;
endpackage
module writer_enum_fields(input clk, reset, mret, input [2:0] data, output [2:0] out);
  import writer_enum_pkg::*;
  typedef struct packed {logic flag; logic [1:0] mode;} status_t;
  status_t q, d;
  always_comb begin
    d = status_t'(data);
    if (mret) begin
      d.mode = Machine;
      d.mode = User;
    end
  end
  always_ff @(posedge clk) if (reset) q <= '0; else q <= d;
  assign out = q;
endmodule
