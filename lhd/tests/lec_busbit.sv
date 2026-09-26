// Bus-expansion naming fixture (lhd/tests/lhd_lec_busbit_test.sh): an 8-bit
// register that synthesis splits into eight one-bit DFF cells named
// `rot[0]..rot[7]` (core/bus_name.hpp). The register is observable at the
// output, so a text-path LEC can only prove it unbounded once state pairing
// regroups the cells into the source register bit-for-bit. The one-bit `busy`
// register keeps its plain name; it drives the output port of the same name,
// so the emitted cell instance is uniquified (`busy_cgen1`) and its read-back
// model state is `busy_cgen1.flop_<n>` (bus_name::cell_state_owner).
module busbit(input clk, input rst, input en, input ld, input [7:0] d, output [7:0] q, output y, output reg busy);
  reg [7:0] rot;
  always @(posedge clk) begin
    if (rst) rot <= 8'h01;
    else if (ld) rot <= d;
    else if (en) rot <= {rot[6:0], rot[7]};
  end
  always @(posedge clk) busy <= !rst && (ld || (en && busy));
  assign q = rot;
  assign y = ^rot;
endmodule
