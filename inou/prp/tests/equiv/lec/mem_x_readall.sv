/*
:lec_set: formal.engine=bmc formal.min_timeout=1 formal.simfail=false
A reset array `hd` is written under an enable that reads `sel_d`, a data
stage with NO reset (it loads only behind the reset valid `sv`, the bedrock
br_delay_valid shape). Right after reset `sel_d` still holds its power-on
value, so a write with `rd_valid` leaves the entry hardware-unknown on the
reference. The array is read WHOLE (a onehot mux over every entry, a
read_all), and that read must carry the entries' unknown plane: the
netlist's un-reset select cells have other names, start at their own free
value, and an equivalent implementation must not be refuted on bits the
reference leaves undefined (br_tracker_linked_list_ctrl `head`).
*/
module onehot_mux(input [1:0] select, input [1:0][3:0] in, output logic [3:0] out);
  always_comb begin
    out = '0;
    for (int i = 0; i < 2; i++) out |= in[i] & {4{select[i]}};
  end
endmodule
module top(input clk, input rst, input addr_valid, input [1:0] sel_in,
           input rd_valid, input [3:0] rd_data, input [1:0] osel, output [3:0] head);
  logic            sv;
  logic [1:0]      sel_d;
  logic [1:0][3:0] hd;
  always_ff @(posedge clk) begin
    if (rst) sv <= 1'b0; else sv <= addr_valid;
    if (sv) sel_d <= sel_in;
  end
  for (genvar i = 0; i < 2; i++) begin : g
    always_ff @(posedge clk) begin
      if (rst) hd[i] <= '0;
      else if (rd_valid && sel_d[i]) hd[i] <= rd_data;
    end
  end
  onehot_mux m(.select(osel), .in(hd), .out(head));
endmodule
