# MemRTLC — translation FAIL

**Reason:** LEC-FAIL (cvc5=REFUTED lgyosys=NONE)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/MemRTLC.sv 

**Detail:**
```
/tmp/sv_MemRTLC/lec.log:counterexample: diff nxt:n:OUT_data(ref=115792089237316195423570985008687907853269984665640564039457584007913129639934 impl=115792089237316195423570985008687907853269984665640564039457584007913129639935), OUT_data(ref=57896044618658097711785492504343953926634992332820282019728792003956564819967 impl=57896044618658097711785492504343953926634992332820282019728792003956564819966) @ n:ce_reg=0, IN_addr=0, IN_nce=0, clk=0, IN_nwe=0, IN_wm=0, IN_data=0, n:we_reg=1, n:wm_reg=0, n:data_reg=0, n:addr_reg=0, n:OUT_data=115792089237316195423570985008687907853269984665640564039457584007913129639934
/tmp/sv_MemRTLC/lec.log:counterexample: diff 
```
