module mux2(input a, input b, input c, input d, input [1:0] sel, output f);

reg f;

always @ (sel or a or b) 
begin:MUX
  if (sel == 2'b0) begin
    f = a;
  end else if (sel == 2'b1) begin 
    f = b;
  end else if (sel == 2'b10) begin
    f = c;
  end else begin
    f = d;
  end
end

endmodule

