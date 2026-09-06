// Empty task calls used by synthesis assertion macros have no hardware effect.
module empty_task(input clk, input en, input [3:0] d, output reg [3:0] q);
  task placeholder;
    begin
      begin end
      ;
    end
  endtask
  always @(posedge clk) begin
    placeholder;
    if (en) begin
      placeholder;
      q <= d + 1;
    end
  end
endmodule
