# SoC — translation FAIL

**Reason:** COMPILE-FAIL (slang reader could not lower)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/SoC.sv  soomrv/stubs/CacheArbiter.stub.sv soomrv/stubs/Core.stub.sv soomrv/stubs/MMIO.stub.sv soomrv/stubs/MemRTL.stub.sv soomrv/stubs/MemRTL1RW.stub.sv soomrv/stubs/MemoryController.stub.sv

**Detail:**
```
     36 "code":"unsupported-expression"	"message":"expression kind 'HierarchicalValue' is not supported by --reader slang yet"
     13 "code":"unsupported-array-read"	"message":"unpacked array read on an unsupported base"
      5 "code":"unsupported-lhs-nesting"	"message":"nested non-variable assignment targets are not supported yet"
      4 "code":"unsupported-lhs"	"message":"assignment-target kind 'HierarchicalValue' is not supported by --reader slang yet"
      3 "code":"unsupported-array-write"	"message":"unpacked array write on an unsupported base"
      1 "code":"unsupported-port-type"	"message":"port 'OUT_ports' has a non-integral type"
      1 "code":"unsupported-port-type"	"message":"port 'IN_portRData' has a non-integral type"
      1 "code":"unsupported-mem-element"	"message":"memory 'CORE_raddr' has a non-integral or multi-dimensional element type"
      1 "code":"unsupported-member"	"message":"module member 'OUT_csrIf' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_mmio' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_mem' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_ict' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_icache' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_ct' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_csr_mmio' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_cache' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'OUT_csrIf' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_mmio' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_mem' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_ict' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_icache' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_ct' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_csr_mmio' is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_cache' is not supported by --reader slang"
      1 "code":"unsupported-instance-kind"	"message":"'IF_mmio' is not a module (interfaces/programs unsupported)"
      1 "code":"unsupported-instance-kind"	"message":"'IF_ict' is not a module (interfaces/programs unsupported)"
      1 "code":"unsupported-instance-kind"	"message":"'IF_icache' is not a module (interfaces/programs unsupported)"
      1 "code":"unsupported-instance-kind"	"message":"'IF_ct' is not a module (interfaces/programs unsupported)"
      1 "code":"unsupported-instance-kind"	"message":"'IF_csr_mmio' is not a module (interfaces/programs unsupported)"
      1 "code":"unsupported-instance-kind"	"message":"'IF_cache' is not a module (interfaces/programs unsupported)"
```
