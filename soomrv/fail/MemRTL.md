# MemRTL — translation FAIL

**Reason:** LEC-FAIL (cvc5=REFUTED lgyosys=NONE)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/MemRTL.sv 

**Detail:**
```
/tmp/sv_MemRTL/lec.log:counterexample: diff nxt:n:OUT_data1(ref=86844066927987146567678238756515930889952488499230423029593188005934847229951 impl=115792089237316195423570985008687907853269984665640564039457584007913129639935) @ n:ce_reg=0, IN_nce=0, clk=0, IN_nwe=0, n:wm_reg=1, IN_data=0, IN_nce1=0, n:OUT_data1=115792089237316195423570985008687907853269984665640564039457584007913129639935, n:data_reg=115792089237316195423570985008687907853269984665640564039457584007913129639935, n:ce1_reg=0, n:addr_reg=127, IN_addr1=0, n:OUT_data=0, IN_addr=0, IN_wm=0, n:we_reg=0, n:addr1_reg=127
/tmp/sv_MemRTL/lec.log:counterexample: diff 
```
