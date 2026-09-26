/*
:type: lec
:lec_top: top
*/
// Small LRU-bit selection kernel: either access path selects one of the same
// two recency bits. Keep this fixture local; external cache designs are QoR
// workloads, not dependencies of the correctness suite.
module top(input [1:0] lru, input [1:0] access, input bank,
           output victim);
  assign victim = bank ? (access[1] ? lru[1] : lru[0])
                       : (access[0] ? lru[1] : lru[0]);
endmodule
