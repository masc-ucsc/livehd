// Two instances of the SAME module created by a genvar loop. SystemVerilog
// tells them apart only by the generate-block index -- `gen_lp[0].u_dl` and
// `gen_lp[1].u_dl` -- so an instance name that drops the generate prefix mints
// TWO Subs, and after `pass color` flattens the design TWO REGISTERS, under one
// hierarchical name.
//
// Everything downstream that keys on a name then has to invent a tiebreak, and
// pass/abc's register read-back invents it on the IMPL SIDE ONLY
// (`<name>__dup1`). The post-synthesis LEC pairs state BY NAME, so the latent
// collision surfaced as a hard rtl-vs-netlist REFUTED for every design with a
// replicated sub-module -- but only under `pass.abc.register=true`, the mode
// that carries flops into ABC so it can optimise logic between them. That was
// 11 of lhdtrack's 149 rtl-vs-netlist rows (bedrock's br_fifo_shared_* family,
// whose br_fifo_shared_read_xbar instantiates br_delay_valid in a genvar loop).
//
// Nets inside a generate block already carry the prefix (`lname_of`); this pins
// that instances do too.
module dl (
    input  logic clk,
    input  logic rst,
    input  logic d,
    output logic q
);
  logic st;
  always_ff @(posedge clk) begin
    if (rst) st <= 1'b0;
    else st <= d;
  end
  assign q = st;
endmodule

module genblk_instance_name (
    input  logic       clk,
    input  logic       rst,
    input  logic [1:0] d,
    output logic [1:0] q
);
  for (genvar i = 0; i < 2; i++) begin : gen_lp
    dl u_dl (.clk(clk), .rst(rst), .d(d[i]), .q(q[i]));
  end
endmodule
