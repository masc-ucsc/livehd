# MMIO — translation FAIL

**Reason:** COMPILE-FAIL (slang reader could not lower)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/MMIO.sv  soomrv/stubs/ACLINT.stub.sv soomrv/stubs/SysCon.stub.sv

**Detail:**
```
     13 "code":"unsupported-expression"	"message":"expression kind 'HierarchicalValue' is not supported by --reader slang yet"
      7 "code":"unsupported-lhs"	"message":"assignment-target kind 'HierarchicalValue' is not supported by --reader slang yet"
      1 "code":"unsupported-member"	"message":"module member 'OUT_csrIf' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_mem' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'OUT_csrIf' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_mem' is not supported by --reader slang"
```
