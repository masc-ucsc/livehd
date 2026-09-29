module \loop_runtime_continue.top (
  input      [7:0] skip,
  output reg [3:0] total
);
  integer i;
  always @* begin
    total = 0;
    for (i = 0; i < 8; i = i + 1) begin
      if (!skip[i])
        total = total + 1'b1;
    end
  end
endmodule
