module function_early_return(
  input  logic       clk,
  input  logic [5:0] dest,
  output logic [1:0] way,
  output logic [1:0] way_from_unreset_state
);
  logic [5:0] unreset_state;

  function automatic logic [1:0] get_way(input logic [5:0] value);
    if (value < 6'd12)
      return 2'd0;
    else if (value < 6'd24)
      return 2'd1;
    else if (value < 6'd36)
      return 2'd2;
    else if (value < 6'd48)
      return 2'd3;
    else if (value < 6'd60)
      return 2'd0;
    return 2'd1;
  endfunction

  assign way = get_way(dest);
  always_ff @(posedge clk)
    unreset_state <= dest;
  assign way_from_unreset_state = get_way(unreset_state);
endmodule
