// Explicit lanes match the generated fixture without requiring loop expansion.
module packed_slice_updates(input clock, reset, input [15:0] wake, input [95:0] data, output [95:0] q);
 reg [15:0][2:0][1:0] dep;
 assign q=dep;
 always @(posedge clock or posedge reset) begin
 if(reset) dep <= 0;
 else begin
if (wake[15]) dep[15] <= (data[90 +: 6] ^ data[0 +: 6]);
else if (|dep[15]) begin
dep[15][2] <= {dep[15][2][0],1'b0};
dep[15][1] <= {dep[15][1][0],1'b0};
dep[15][0] <= {dep[15][0][0],1'b0};
end
if (wake[14]) dep[14] <= (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]));
else if (|dep[14]) begin
dep[14][2] <= {dep[14][2][0],1'b0};
dep[14][1] <= {dep[14][1][0],1'b0};
dep[14][0] <= {dep[14][0][0],1'b0};
end
if (wake[13]) dep[13] <= (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])));
else if (|dep[13]) begin
dep[13][2] <= {dep[13][2][0],1'b0};
dep[13][1] <= {dep[13][1][0],1'b0};
dep[13][0] <= {dep[13][0][0],1'b0};
end
if (wake[12]) dep[12] <= (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))));
else if (|dep[12]) begin
dep[12][2] <= {dep[12][2][0],1'b0};
dep[12][1] <= {dep[12][1][0],1'b0};
dep[12][0] <= {dep[12][0][0],1'b0};
end
if (wake[11]) dep[11] <= (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))));
else if (|dep[11]) begin
dep[11][2] <= {dep[11][2][0],1'b0};
dep[11][1] <= {dep[11][1][0],1'b0};
dep[11][0] <= {dep[11][0][0],1'b0};
end
if (wake[10]) dep[10] <= (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))));
else if (|dep[10]) begin
dep[10][2] <= {dep[10][2][0],1'b0};
dep[10][1] <= {dep[10][1][0],1'b0};
dep[10][0] <= {dep[10][0][0],1'b0};
end
if (wake[9]) dep[9] <= (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))))));
else if (|dep[9]) begin
dep[9][2] <= {dep[9][2][0],1'b0};
dep[9][1] <= {dep[9][1][0],1'b0};
dep[9][0] <= {dep[9][0][0],1'b0};
end
if (wake[8]) dep[8] <= (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))))));
else if (|dep[8]) begin
dep[8][2] <= {dep[8][2][0],1'b0};
dep[8][1] <= {dep[8][1][0],1'b0};
dep[8][0] <= {dep[8][0][0],1'b0};
end
if (wake[7]) dep[7] <= (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))))))));
else if (|dep[7]) begin
dep[7][2] <= {dep[7][2][0],1'b0};
dep[7][1] <= {dep[7][1][0],1'b0};
dep[7][0] <= {dep[7][0][0],1'b0};
end
if (wake[6]) dep[6] <= (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))))))));
else if (|dep[6]) begin
dep[6][2] <= {dep[6][2][0],1'b0};
dep[6][1] <= {dep[6][1][0],1'b0};
dep[6][0] <= {dep[6][0][0],1'b0};
end
if (wake[5]) dep[5] <= (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))))))))));
else if (|dep[5]) begin
dep[5][2] <= {dep[5][2][0],1'b0};
dep[5][1] <= {dep[5][1][0],1'b0};
dep[5][0] <= {dep[5][0][0],1'b0};
end
if (wake[4]) dep[4] <= (data[24 +: 6] ^ (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))))))))));
else if (|dep[4]) begin
dep[4][2] <= {dep[4][2][0],1'b0};
dep[4][1] <= {dep[4][1][0],1'b0};
dep[4][0] <= {dep[4][0][0],1'b0};
end
if (wake[3]) dep[3] <= (data[18 +: 6] ^ (data[24 +: 6] ^ (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))))))))))));
else if (|dep[3]) begin
dep[3][2] <= {dep[3][2][0],1'b0};
dep[3][1] <= {dep[3][1][0],1'b0};
dep[3][0] <= {dep[3][0][0],1'b0};
end
if (wake[2]) dep[2] <= (data[12 +: 6] ^ (data[18 +: 6] ^ (data[24 +: 6] ^ (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))))))))))));
else if (|dep[2]) begin
dep[2][2] <= {dep[2][2][0],1'b0};
dep[2][1] <= {dep[2][1][0],1'b0};
dep[2][0] <= {dep[2][0][0],1'b0};
end
if (wake[1]) dep[1] <= (data[6 +: 6] ^ (data[12 +: 6] ^ (data[18 +: 6] ^ (data[24 +: 6] ^ (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6])))))))))))))));
else if (|dep[1]) begin
dep[1][2] <= {dep[1][2][0],1'b0};
dep[1][1] <= {dep[1][1][0],1'b0};
dep[1][0] <= {dep[1][0][0],1'b0};
end
if (wake[0]) dep[0] <= (data[0 +: 6] ^ (data[6 +: 6] ^ (data[12 +: 6] ^ (data[18 +: 6] ^ (data[24 +: 6] ^ (data[30 +: 6] ^ (data[36 +: 6] ^ (data[42 +: 6] ^ (data[48 +: 6] ^ (data[54 +: 6] ^ (data[60 +: 6] ^ (data[66 +: 6] ^ (data[72 +: 6] ^ (data[78 +: 6] ^ (data[84 +: 6] ^ (data[90 +: 6] ^ data[0 +: 6]))))))))))))))));
else if (|dep[0]) begin
dep[0][2] <= {dep[0][2][0],1'b0};
dep[0][1] <= {dep[0][1][0],1'b0};
dep[0][0] <= {dep[0][0][0],1'b0};
end
end
end
endmodule
