circuit package_Anon :
  module package_Anon : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip x : {ppn : UInt<20>, u : UInt<1>, g : UInt<1>, ae : UInt<1>, sw : UInt<1>, sx : UInt<1>, sr : UInt<1>, pw : UInt<1>, px : UInt<1>, pr : UInt<1>, pal : UInt<1>, paa : UInt<1>, eff : UInt<1>, c : UInt<1>, fragmented_superpage : UInt<1>}, y : {ppn : UInt<20>, u : UInt<1>, g : UInt<1>, ae : UInt<1>, sw : UInt<1>, sx : UInt<1>, sr : UInt<1>, pw : UInt<1>, px : UInt<1>, pr : UInt<1>, pal : UInt<1>, paa : UInt<1>, eff : UInt<1>, c : UInt<1>, fragmented_superpage : UInt<1>}}
    
    clock is invalid
    reset is invalid
    io is invalid
    io.y <- io.x @[package.scala 218:12]
    
