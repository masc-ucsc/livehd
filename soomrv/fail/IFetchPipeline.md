# IFetchPipeline — translation FAIL

**Reason:** COMPILE-FAIL (slang reader could not lower)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/IFetchPipeline.sv  soomrv/stubs/BranchHandler.stub.sv soomrv/stubs/FIFO.stub.sv soomrv/stubs/InstrAligner.stub.sv soomrv/stubs/OHEncoder.stub.sv soomrv/stubs/TLB.stub.sv

**Detail:**
```
     18 "code":"unsupported-lhs"	"message":"assignment-target kind 'HierarchicalValue' is not supported by --reader slang yet"
      4 "code":"unsupported-lhs-nesting"	"message":"nested non-variable assignment targets are not supported yet"
      4 "code":"unsupported-expression"	"message":"expression kind 'HierarchicalValue' is not supported by --reader slang yet"
      1 "code":"unsupported-member"	"message":"module member 'IF_ict' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_icache' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-lhs"	"message":"assignment-target kind 'SimpleAssignmentPattern' is not supported by --reader slang yet"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_ict' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_icache' is not supported by --reader slang"
      1 "code":"unsupported-assignment-pattern"	"message":"only packed (integral) '{...} assignment patterns are supported by --reader slang yet"
```
