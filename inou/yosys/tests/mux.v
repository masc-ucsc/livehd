module mux(input a, input b, input sel, output f);

reg f;

always @ (sel or a or b) 
begin:MUX
  if (sel == 1'b0) begin
    f = a;
  end else begin 
    f = b;
  end
end

endmodule

