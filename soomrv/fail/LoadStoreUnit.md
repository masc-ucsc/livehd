# LoadStoreUnit — translation FAIL

**Reason:** COMPILE-FAIL (slang reader could not lower)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/LoadStoreUnit.sv  soomrv/stubs/BypassLSU.stub.sv soomrv/stubs/OHEncoder.stub.sv

**Detail:**
```
     17 "code":"unsupported-lhs-nesting"	"message":"nested non-variable assignment targets are not supported yet"
     14 "code":"unsupported-lhs"	"message":"assignment-target kind 'HierarchicalValue' is not supported by --reader slang yet"
      5 "code":"unsupported-expression"	"message":"expression kind 'HierarchicalValue' is not supported by --reader slang yet"
      4 "code":"unsupported-array-read"	"message":"unpacked array read on an unsupported base"
      3 "code":"unsupported-array-write"	"message":"unpacked array write on an unsupported base"
      1 "code":"unsupported-mem-element"	"message":"memory 'ldOps' has a non-integral or multi-dimensional element type"
      1 "code":"unsupported-member"	"message":"module member 'IF_mmio' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_cache' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_mmio' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_cache' is not supported by --reader slang"
      1 "code":"unknown-module"	"message":"instance 'loadResBuf' refers to an unknown module (blackboxes are not supported by --reader slang)"
```
