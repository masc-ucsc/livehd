// Golden for verilog_port_named_clock: Verilog ports whose NAMES are Pyrope
// type words (`Clock`, `Reset`). In Pyrope they are written backticked
// (`` `Clock` ``) -- an ordinary name in every position -- and LNAST keeps the
// quotes while the netlist port is bare `Clock`. The implicit clock/reset and
// a `clock_pin=`Clock`` used to miss that port: an `'hx` clock and a dropped
// reset (exit 0), or "names clock_pin '`Clock`' but has no such input". The
// v2prp2v leg re-reads the writer's Pyrope for exactly this.
module verilog_port_named_clock(input Clock, input Reset, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  always @(posedge Clock) begin
    if (Reset) r <= 8'd9;
    else r <= d;
  end
  assign q = r;
endmodule
