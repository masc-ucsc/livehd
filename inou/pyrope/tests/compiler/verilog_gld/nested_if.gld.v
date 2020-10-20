module nested_if_gld (
  input  [3:0] a,
  input  [3:0] b,
  input  [3:0] c,
  input  [3:0] d,
  input  [3:0] e,
  input  [3:0] f,
  output [4:0] o1,
  output [4:0] o2
);

reg [3:0] x;
reg [3:0] y;


always @ (*) begin
  x = a;
  y = 5;
  if ($signed(a) > $signed(1)) begin
    x = e; 
    if ($signed(a) > $signed(2)) begin
      x = b;
    end 
    else if ($signed(($signed(a) + $signed(1))) > $signed(3)) begin 
      x = c;
    end 
    else begin
      x = d;
    end
    y = e;
  end 
  else begin
    x = f;
  end
end

assign o1 = $signed(x) + $signed(a);
assign o2 = $signed(y) + $signed(a);

endmodule
