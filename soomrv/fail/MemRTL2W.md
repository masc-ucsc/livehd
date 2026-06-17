# MemRTL2W — translation FAIL

**Reason:** LEC-FAIL (cvc5=REFUTED lgyosys=NONE)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/MemRTL2W.sv 

**Detail:**
```
/tmp/sv_MemRTL2W/lec.log:counterexample: diff OUT_data1(ref=255 impl=254) @ IN_nwe1=0, n:ce_reg=0, IN_nce=0, clk=0, IN_nwe=0, n:wm_reg=1, IN_data=0, IN_nce1=0, IN_addr1=0, n:data_reg=255, IN_wm1=0, n:we1_reg=0, n:ce1_reg=0, n:addr_reg=511, n:OUT_data1=57896044618658097711785492504343953926634992332820282019728792003956564820222, n:data1_reg=0, n:OUT_data=0, IN_addr=0, n:wm1_reg=0, IN_wm=0, n:we_reg=0, n:addr1_reg=511, IN_data1=0, n:dbgMultiple=0
/tmp/sv_MemRTL2W/lec.log:counterexample: diff OUT_data1(ref=255 impl=254) @ IN_nwe1=0, 
```
