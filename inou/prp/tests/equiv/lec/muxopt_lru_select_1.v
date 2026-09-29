/*
:lec_expect: proven
*/
module top(input [1:0] lru, input [1:0] access, input bank,
           output victim);
  wire select = bank ? access[1] : access[0];
  assign victim = select ? lru[1] : lru[0];
endmodule
