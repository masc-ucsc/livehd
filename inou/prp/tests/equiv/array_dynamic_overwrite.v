module array_dynamic_overwrite(input logic en, idx, input logic [3:0] value,
    output wire [7:0] current, original);
  assign current = !en ? 8'h52 : (idx ? {value, 4'h2} : {4'h5, value});
  assign original = 8'h52;
endmodule
