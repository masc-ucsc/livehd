module latch(input d, input c, output q);

always_latch begin
  if(c == 1) begin
    q <= d;
  end
end
endmodule

