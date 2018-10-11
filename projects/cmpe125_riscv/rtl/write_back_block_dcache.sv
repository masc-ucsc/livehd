module write_back_block_dcache(
    input clk,
    input [64-1:0] fpx10559__index__12128,
    input [64-1:0] __tid541__12462,
    input [64-1:0] fpx10696__index__13264,
    input [64-1:0] fpx10696__index__14185,
    input [64-1:0] __tid1422__14689,
    input [64-1:0] __tid1518__15422,
    input [64-1:0] fpx10696__index__13244,
    input [64-1:0] __tid17237,
    input [64-1:0] __tid17239,
    output [64-1:0] __tid464__12131,
    output [64-1:0] __tid543__12464,
    output [64-1:0] __tid1203__13265,
    output [64-1:0] __tid1323__14186,
    output [64-1:0] __tid1424__14691,
    output [64-1:0] __tid17011__17374,
    output [64-1:0] __tid17017__17377,
    output [64-1:0] __tid17021__17379,
    output [64-1:0] __tid17027__17382,
    output [64-1:0] __tid17031__17384,
    output [64-1:0] __tid17037__17387,
    output [64-1:0] __tid17041__17389,
    output [64-1:0] __tid17047__17392,
    output [64-1:0] __tid17051__17394,
    output [64-1:0] __tid17055__17396,
    output [64-1:0] __tid17059__17398);

  logic [64-1:0] dcache[2048-1:0] /*verilator public*/;
  always @(posedge clk) begin
     dcache[fpx10696__index__13244[10:0]] <= __tid17237;
     dcache[__tid1518__15422[10:0]] <= __tid17239;
  end

  assign __tid464__12131 = dcache[fpx10559__index__12128[10:0]];
  assign __tid543__12464 = dcache[__tid541__12462[10:0]];
  assign __tid1203__13265 = dcache[fpx10696__index__13264[10:0]];
  assign __tid1323__14186 = dcache[fpx10696__index__14185[10:0]];
  assign __tid1424__14691 = dcache[__tid1422__14689[10:0]];
  assign __tid17011__17374 = dcache[__tid1518__15422[10:0]];
  assign __tid17017__17377 = dcache[fpx10696__index__13244[10:0]];
  assign __tid17021__17379 = dcache[__tid1518__15422[10:0]];
  assign __tid17027__17382 = dcache[fpx10696__index__13244[10:0]];
  assign __tid17031__17384 = dcache[__tid1518__15422[10:0]];
  assign __tid17037__17387 = dcache[fpx10696__index__13244[10:0]];
  assign __tid17041__17389 = dcache[__tid1518__15422[10:0]];
  assign __tid17047__17392 = dcache[fpx10696__index__13244[10:0]];
  assign __tid17051__17394 = dcache[__tid1518__15422[10:0]];
  assign __tid17055__17396 = dcache[fpx10696__index__13244[10:0]];
  assign __tid17059__17398 = dcache[__tid1518__15422[10:0]];
endmodule