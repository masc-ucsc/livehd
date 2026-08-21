typedef struct packed {
  int unsigned nr_rules;
} deferred_cfg_t;

typedef struct packed {
  logic [7:0] data;
  logic       valid;
} deferred_lane_t;

module deferred_cva6_forms (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        en_i,
  input  logic [0:0]  row_i,
  input  logic [0:0]  lane_i,
  input  logic [1:0]  event_i,
  input  logic [7:0]  data_i,
  output logic [7:0]  data_o,
  output logic        valid_o,
  output logic        inside_o,
  output logic [31:0] power_o,
  output logic        event_o
);
  localparam deferred_cfg_t Cfg = '{nr_rules: 3};

  deferred_lane_t rows_q[3:0];
  deferred_lane_t rows_d[3:0];
  deferred_lane_t btb_q[1:0][1:0];
  deferred_lane_t btb_d[1:0][1:0];
  logic events[3:1];

  function automatic logic inside_rules(deferred_cfg_t cfg, logic [1:0] address);
    logic [7:0] pass;
    pass = '0;
    for (int unsigned k = 0; k < cfg.nr_rules; k++) begin
      pass[k] = address == k;
    end
    return |pass;
  endfunction

  function automatic logic [31:0] add_power(deferred_cfg_t cfg, logic [31:0] value);
    return value + 2 ** cfg.nr_rules;
  endfunction

  always_comb begin
    rows_d = rows_q;
    if (en_i) rows_d[{row_i, lane_i}] = '{data: data_i, valid: 1'b1};

    btb_d = btb_q;
    if (en_i) begin
      btb_d[row_i][lane_i].data  = data_i;
      btb_d[row_i][lane_i].valid = 1'b1;
    end

    events[3:1] = '{default: 0};
    events[1] = en_i;
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      rows_q <= '{default: 0};
      for (int i = 0; i < 2; i++) btb_q[i] <= '{default: 0};
    end else begin
      rows_q <= rows_d;
      btb_q <= btb_d;
    end
  end

  assign data_o   = rows_q[{row_i, lane_i}].data ^ btb_q[row_i][lane_i].data;
  assign valid_o  = rows_q[{row_i, lane_i}].valid ^ btb_q[row_i][lane_i].valid;
  assign inside_o = inside_rules(Cfg, event_i);
  assign power_o  = add_power(Cfg, data_i);
  assign event_o  = events[event_i];
endmodule
