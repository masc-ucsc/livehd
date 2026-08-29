module generated_packed_stage_chain (
    input  logic [3:0] in,
    input  logic [1:0] rotate,
    output logic [3:0] out
);
  logic [2:0][3:0] stages;

  assign stages[0] = in;
  assign out = stages[2];

  for (genvar stage = 0; stage < 2; stage++) begin : gen_stages
    localparam int Amount = 1 << stage;
    wire [3:0] stage_in = stages[stage];
    wire [3:0] stage_rot = (stage_in << Amount) | (stage_in >> (4 - Amount));
    assign stages[stage + 1] = rotate[stage] ? stage_rot : stage_in;
  end
endmodule
