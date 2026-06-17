# BypassLSU — translation FAIL

**Reason:** LEC-FAIL (cvc5=REFUTED lgyosys=NONE)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/BypassLSU.sv 

**Detail:**
```
/tmp/sv_BypassLSU/lec.log:counterexample: diff nxt:n:OUT_memc(ref=53919893334301279589334030174039261347274288845081144962207220498431 impl=53919893334301279589334030174039261347274288845081144962207220498415), OUT_memc(ref=26959946667150639794667015087019630673637142005279229062203966488548 impl=26959946667150639794667015087019630673637144422540572481103610249215) @ n:activeLd=4064, IN_uopLdEn=1, rst=0, clk=0, IN_branch=266338304, IN_uopLd=2535301200456458802967636606971, IN_uopStEn=0, IN_uopSt=0, n:OUT_memc=53919893334301279589334030174039261347274288845081144962207220498431, n:state=0, n:OUT_ldData=0, IN_memc=8, IN_ldStall=0
/tmp/sv_BypassLSU/lec.log:counterexample: diff 
```
