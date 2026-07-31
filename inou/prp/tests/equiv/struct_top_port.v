// Golden with a PACKED STRUCT ON THE TOP INTERFACE — the shape the equiv
// corpus otherwise has none of (instance_out_struct_ident's struct is on an
// INTERNAL instance; its top is flat). It guards the `flat_top_io` contract:
// a struct is a bundle everywhere inside LiveHD, but the emitted TOP interface
// must stay a packed bus so the generated netlist is a drop-in replacement for
// this module and yosys can miter the two (prp-v2prp2v-struct_top_port).
package stp_pkg;
  typedef struct packed {
    logic       fp;
    logic [4:0] addr;
    logic       thread_id;
  } stp_dest_t;
endpackage

module stp_kid(input logic clk, input logic rst_ni,
               input stp_pkg::stp_dest_t din, output stp_pkg::stp_dest_t dout);
  always_ff @(posedge clk) begin
    if (!rst_ni) dout <= '0;
    else         dout <= din;
  end
endmodule

module struct_top_port(input logic clk, input logic rst_ni,
                       input  stp_pkg::stp_dest_t din,
                       output stp_pkg::stp_dest_t dout);
  stp_kid u(.clk(clk), .rst_ni(rst_ni), .din(din), .dout(dout));
endmodule
