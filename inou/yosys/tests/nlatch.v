module nlatch(input d, input c, output q);

always_latch begin
  if(c == 0) begin
    q <= d;
  end
end
endmodule

