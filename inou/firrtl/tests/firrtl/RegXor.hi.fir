circuit RegXor :
  module RegXor :
    input clock : Clock
    input reset : UInt<1>
    input inp : UInt<16>
    output out : UInt<16>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x)
    out <= x
    x <= xor(inp, UInt<16>("hffff"))