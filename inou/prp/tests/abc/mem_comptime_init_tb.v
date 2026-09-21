module tb;
  reg clock=0, reset=0, i=0, j=0;
  wire [3:0] z;
  mci dut(.clock(clock), .reset(reset), .i(i), .j(j), .z(z));
  task check_entries;
    begin
      for (integer a=0; a<4; a=a+1) begin
        i=a/2; j=a%2; #1;
        if (z !== a+1) $fatal(1,"entry %d expected %d got %d",a,a+1,z);
      end
    end
  endtask
  initial begin
    // All four distinct contents must exist before the first clock edge.
    check_entries;
    #1; clock=1; #1; clock=0;
    reset=1;
    repeat (4) begin
      #1; clock=1; #1; clock=0;
      check_entries;
    end
    reset=0;
    repeat (8) begin
      #1; clock=1; #1; clock=0;
      check_entries;
    end
    $finish;
  end
endmodule
