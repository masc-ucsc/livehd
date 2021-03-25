module reg__q_pin (
  input        inp, 
  input        clock,
  input        reset,
  output [2:0] out
);

wire [2:0] out_next = inp == 1 ? 3'd4 + 3'd1 : 3'd4;

always @ (posedge clock) begin 
  if (reset) begin
    out <= 'h0;
  end else begin
    out <= out_next;
  end
end
endmodule
