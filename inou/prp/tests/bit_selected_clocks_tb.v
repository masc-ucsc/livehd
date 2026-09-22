module tb;
  reg [0:2] clocks = 0;
  reg gate = 1, clr_ = 1;
  reg [2:0] data = 0, enable = 7;
  wire [2:0] q;
  bit_selected_clocks dut(.clk(1'b1), .gate0(clocks[2]), .gate1(clocks[1]), .gate2(clocks[0]),
                          .gate(gate), .clr_(clr_), .data(data), .enable(enable), .q(q));

  task check(input [2:0] expected);
    begin
      #1;
      if (q !== expected)
        $fatal(1, "time=%0t expected=%b got=%b", $time, expected, q);
    end
  endtask

  initial begin
    #1 clr_ = 0;
    check(3'b010); // asynchronous assertion, no clock edge
    clr_ = 1;
    data = 3'b101;
    clocks[2] = 1;
    check(3'b011); // only bit 0 sees its rising edge
    clocks[0] = 1;
    check(3'b111); // only bit 2 sees its rising edge
    clocks[1] = 1;
    check(3'b111); // bit 1 responds only to falling edges
    clocks[1] = 0;
    check(3'b101);
    data = 3'b010;
    clocks = 0;
    check(3'b101);
    enable[0] = 0;
    clocks[2] = 1;
    check(3'b101); // disabled bit holds while other clocks stay still
    clocks[0] = 1;
    check(3'b001);
    clocks[1] = 1;
    check(3'b001);
    gate = 0;
    check(3'b011); // gating creates the falling edge for bit 1
    enable = 7;
    data = 0;
    #1 gate = 1;
    check(3'b010); // rising edges for bits 0 and 2 only
    clr_ = 0;
    check(3'b010);
    data = 7;
    clocks = 0;
    check(3'b010); // reset wins over a falling clock edge
    clocks = 7;
    check(3'b010); // reset wins over rising clock edges
    clr_ = 1;
    check(3'b010); // releasing reset is not a clock edge
    $display("independent bit clocks and asynchronous reset passed");
    $finish;
  end
endmodule
