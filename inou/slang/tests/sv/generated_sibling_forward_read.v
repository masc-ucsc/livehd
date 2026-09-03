module generated_sibling_forward_read (
    input  logic [3:0] grant,
    input  logic [3:0][3:0] state_reg,
    output logic [3:0][3:0] state_next
);
  logic [3:0][3:0] state;

  for (genvar i = 0; i < 4; i++) begin : gen_row
    for (genvar j = 0; j < 4; j++) begin : gen_col
      if (i < j) begin : gen_upper
        assign state_next[i][j] = grant[i] ? 1'b0 : grant[j] ? 1'b1 : state[i][j];
        assign state[i][j] = state_reg[i][j];
      end else begin : gen_other
        assign state_next[i][j] = 1'b0;
        assign state[i][j] = 1'b0;
      end
    end
  end
endmodule
