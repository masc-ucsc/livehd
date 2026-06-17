# Core — translation FAIL

**Reason:** COMPILE-FAIL (slang reader could not lower)

**Read set:** /mada/users/renau/projs/soomrv/repo/src/Config.sv /mada/users/renau/projs/soomrv/repo/src/Include.sv /mada/users/renau/projs/soomrv/repo/src/Core.sv  soomrv/stubs/AGU.stub.sv soomrv/stubs/BranchSelector.stub.sv soomrv/stubs/CSR.stub.sv soomrv/stubs/CacheLineManager.stub.sv soomrv/stubs/DataPrefetch.stub.sv soomrv/stubs/Divide.stub.sv soomrv/stubs/FDiv.stub.sv soomrv/stubs/FMul.stub.sv soomrv/stubs/FPU.stub.sv soomrv/stubs/IFetch.stub.sv soomrv/stubs/InstrDecoder.stub.sv soomrv/stubs/IntALU.stub.sv soomrv/stubs/IssueQueue.stub.sv soomrv/stubs/Load.stub.sv soomrv/stubs/LoadBuffer.stub.sv soomrv/stubs/LoadSelector.stub.sv soomrv/stubs/LoadStoreUnit.stub.sv soomrv/stubs/Multiply.stub.sv soomrv/stubs/PageWalker.stub.sv soomrv/stubs/RFReadMux.stub.sv soomrv/stubs/ROB.stub.sv soomrv/stubs/RegFile.stub.sv soomrv/stubs/Rename.stub.sv soomrv/stubs/ResultFlagsSplit.stub.sv soomrv/stubs/StoreDataIQ.stub.sv soomrv/stubs/StoreDataLoad.stub.sv soomrv/stubs/StoreQueue.stub.sv soomrv/stubs/StoreQueueBackend.stub.sv soomrv/stubs/TLB.stub.sv soomrv/stubs/TValSelect.stub.sv soomrv/stubs/TrapHandler.stub.sv

**Detail:**
```
      9 "code":"unsupported-array-read"	"message":"only single-element reads of unpacked arrays are supported by --reader slang"
      3 "code":"unsupported-member"	"message":"module member 'IF_mmio' (kind InterfacePort) is not supported by --reader slang"
      3 "code":"unsupported-interface-port"	"message":"interface port 'IF_mmio' is not supported by --reader slang"
      2 "code":"unsupported-member"	"message":"module member 'IF_ict' (kind InterfacePort) is not supported by --reader slang"
      2 "code":"unsupported-member"	"message":"module member 'IF_icache' (kind InterfacePort) is not supported by --reader slang"
      2 "code":"unsupported-member"	"message":"module member 'IF_ct' (kind InterfacePort) is not supported by --reader slang"
      2 "code":"unsupported-member"	"message":"module member 'IF_cache' (kind InterfacePort) is not supported by --reader slang"
      2 "code":"unsupported-interface-port"	"message":"interface port 'IF_ict' is not supported by --reader slang"
      2 "code":"unsupported-interface-port"	"message":"interface port 'IF_icache' is not supported by --reader slang"
      2 "code":"unsupported-interface-port"	"message":"interface port 'IF_ct' is not supported by --reader slang"
      2 "code":"unsupported-interface-port"	"message":"interface port 'IF_cache' is not supported by --reader slang"
      1 "code":"unsupported-member"	"message":"module member 'IF_csr_mmio' (kind InterfacePort) is not supported by --reader slang"
      1 "code":"unsupported-interface-port"	"message":"interface port 'IF_csr_mmio' is not supported by --reader slang"
```
