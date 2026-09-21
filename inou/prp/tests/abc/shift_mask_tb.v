module tb;
  reg [7:0] data;
  reg [15:0] amount;
  wire [7:0] masked, shifted, ref_masked, ref_shifted;
  shift_mask dut(.*);
  reference golden(.data(data),.amount(amount),.masked(ref_masked),.shifted(ref_shifted));
  task check;
    begin
      #1;
      if (masked !== ref_masked || shifted !== ref_shifted)
        $fatal(1,"data=%h amount=%d masked=%h/%h shifted=%h/%h",data,amount,masked,ref_masked,shifted,ref_shifted);
    end
  endtask
  initial begin
    for (integer s=0; s<65536; s=s+1) begin amount=s; data=s*29+13; check; end
    for (integer s=0; s<10; s=s+1) begin
      for (integer d=0; d<256; d=d+1) begin amount=s; data=d; check; end
    end
    $finish;
  end
endmodule
