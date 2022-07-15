circuit DCombinMemPort :
  module DCombinMemPort : 
    input clock : Clock
    input reset : UInt<1>
    output io : {pipeline : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, flip writedata : UInt<32>, flip memread : UInt<1>, flip memwrite : UInt<1>, flip maskmode : UInt<2>, flip sext : UInt<1>, readdata : UInt<32>}, flip bus : {flip request : {flip ready : UInt<1>, valid : UInt<1>, bits : {address : UInt<32>, writedata : UInt<32>, operation : UInt<2>}}, response : {valid : UInt<1>, bits : {data : UInt<32>}}}}
    
    wire _T : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, flip writedata : UInt<32>, flip memread : UInt<1>, flip memwrite : UInt<1>, flip maskmode : UInt<2>, flip sext : UInt<1>, readdata : UInt<32>} 
    _T.readdata <= UInt<32>("h00") 
    _T.sext <= UInt<1>("h00") 
    _T.maskmode <= UInt<2>("h00") 
    _T.memwrite <= UInt<1>("h00") 
    _T.memread <= UInt<1>("h00") 
    _T.writedata <= UInt<32>("h00") 
    _T.good <= UInt<1>("h00") 
    _T.valid <= UInt<1>("h00") 
    _T.address <= UInt<32>("h00") 
    io.pipeline.readdata <= _T.readdata 
    _T.sext <= io.pipeline.sext 
    _T.maskmode <= io.pipeline.maskmode 
    _T.memwrite <= io.pipeline.memwrite 
    _T.memread <= io.pipeline.memread 
    _T.writedata <= io.pipeline.writedata 
    io.pipeline.good <= _T.good 
    _T.valid <= io.pipeline.valid 
    _T.address <= io.pipeline.address 
    io.bus.response.bits.data is invalid 
    io.bus.response.valid is invalid 
    io.bus.request.bits.operation is invalid 
    io.bus.request.bits.writedata is invalid 
    io.bus.request.bits.address is invalid 
    io.bus.request.valid is invalid 
    io.bus.request.ready is invalid 
    io.pipeline.good <= io.bus.response.valid 
    io.pipeline.good <= UInt<1>("h01") 
    node _T_1 = or(io.pipeline.memread, io.pipeline.memwrite) 
    node _T_2 = and(io.pipeline.valid, _T_1) 
    when _T_2 : 
      node _T_3 = and(io.pipeline.memread, io.pipeline.memwrite) 
      node _T_4 = eq(_T_3, UInt<1>("h00")) 
      node _T_5 = asUInt(reset) 
      node _T_6 = or(_T_4, _T_5) 
      node _T_7 = eq(_T_6, UInt<1>("h00")) 
      when _T_7 : 
        printf(clock, UInt<1>(1), "Assertion failed\n    at memory-combin-ports.scala:46 assert(!(io.pipeline.memread && io.pipeline.memwrite))\n") 
        stop(clock, UInt<1>(1), 1) 
        skip 
      io.bus.request.bits.address <= io.pipeline.address 
      io.bus.request.valid <= UInt<1>("h01") 
      when io.pipeline.memwrite : 
        io.bus.request.bits.operation <= UInt<2>("h02") 
        skip 
      else : 
        io.bus.request.bits.operation <= UInt<1>("h00") 
        skip 
      skip 
    else : 
      io.bus.request.valid <= UInt<1>("h00") 
      skip 
    when io.bus.response.valid : 
      when io.pipeline.memwrite : 
        wire _T_8 : UInt<32> 
        node _T_9 = neq(io.pipeline.maskmode, UInt<2>("h02")) 
        when _T_9 : 
          node _T_10 = bits(io.pipeline.address, 1, 0) 
          wire _T_11 : UInt<32> 
          _T_11 <= io.bus.response.bits.data 
          node _T_12 = eq(io.pipeline.maskmode, UInt<1>("h00")) 
          when _T_12 : 
            node _T_13 = eq(_T_10, UInt<1>("h00")) 
            when _T_13 : 
              node _T_14 = bits(_T_11, 31, 8) 
              node _T_15 = bits(io.pipeline.writedata, 7, 0) 
              node _T_16 = cat(_T_14, _T_15) 
              _T_8 <= _T_16 
              skip 
            else : 
              node _T_17 = eq(_T_10, UInt<1>("h01")) 
              when _T_17 : 
                node _T_18 = bits(_T_11, 31, 16) 
                node _T_19 = bits(io.pipeline.writedata, 15, 8) 
                node _T_20 = bits(_T_11, 7, 0) 
                node _T_21 = cat(_T_19, _T_20) 
                node _T_22 = cat(_T_18, _T_21) 
                _T_8 <= _T_22 
                skip 
              else : 
                node _T_23 = eq(_T_10, UInt<2>("h02")) 
                when _T_23 : 
                  node _T_24 = bits(_T_11, 31, 24) 
                  node _T_25 = bits(io.pipeline.writedata, 23, 16) 
                  node _T_26 = bits(_T_11, 15, 0) 
                  node _T_27 = cat(_T_25, _T_26) 
                  node _T_28 = cat(_T_24, _T_27) 
                  _T_8 <= _T_28 
                  skip 
                else : 
                  node _T_29 = bits(io.pipeline.writedata, 31, 24) 
                  node _T_30 = bits(_T_11, 23, 0) 
                  node _T_31 = cat(_T_29, _T_30) 
                  _T_8 <= _T_31 
                  skip 
            skip 
          else : 
            node _T_32 = eq(_T_10, UInt<1>("h00")) 
            when _T_32 : 
              node _T_33 = bits(_T_11, 31, 16) 
              node _T_34 = bits(io.pipeline.writedata, 15, 0) 
              node _T_35 = cat(_T_33, _T_34) 
              _T_8 <= _T_35 
              skip 
            else : 
              node _T_36 = bits(io.pipeline.writedata, 31, 16) 
              node _T_37 = bits(_T_11, 15, 0) 
              node _T_38 = cat(_T_36, _T_37) 
              _T_8 <= _T_38 
              skip 
            skip 
          skip 
        else : 
          _T_8 <= io.pipeline.writedata 
          skip 
        io.bus.request.bits.writedata <= _T_8 
        skip 
      else : 
        when io.pipeline.memread : 
          wire _T_39 : UInt<32> 
          wire _T_40 : UInt<32> 
          node _T_41 = bits(io.pipeline.address, 1, 0) 
          node _T_42 = eq(io.pipeline.maskmode, UInt<1>("h00")) 
          when _T_42 : 
            node _T_43 = mul(_T_41, UInt<4>("h08")) 
            node _T_44 = dshr(io.bus.response.bits.data, _T_43) 
            node _T_45 = and(_T_44, UInt<8>("h0ff")) 
            _T_39 <= _T_45 
            skip 
          else : 
            node _T_46 = eq(io.pipeline.maskmode, UInt<1>("h01")) 
            when _T_46 : 
              node _T_47 = mul(_T_41, UInt<4>("h08")) 
              node _T_48 = dshr(io.bus.response.bits.data, _T_47) 
              node _T_49 = and(_T_48, UInt<16>("h0ffff")) 
              _T_39 <= _T_49 
              skip 
            else : 
              _T_39 <= io.bus.response.bits.data 
              skip 
          when io.pipeline.sext : 
            node _T_50 = eq(io.pipeline.maskmode, UInt<1>("h00")) 
            when _T_50 : 
              node _T_51 = bits(_T_39, 7, 7) 
              node _T_52 = bits(_T_51, 0, 0) 
              node _T_53 = mux(_T_52, UInt<24>("h0ffffff"), UInt<24>("h00")) 
              node _T_54 = bits(_T_39, 7, 0) 
              node _T_55 = cat(_T_53, _T_54) 
              _T_40 <= _T_55 
              skip 
            else : 
              node _T_56 = eq(io.pipeline.maskmode, UInt<1>("h01")) 
              when _T_56 : 
                node _T_57 = bits(_T_39, 15, 15) 
                node _T_58 = bits(_T_57, 0, 0) 
                node _T_59 = mux(_T_58, UInt<16>("h0ffff"), UInt<16>("h00")) 
                node _T_60 = bits(_T_39, 15, 0) 
                node _T_61 = cat(_T_59, _T_60) 
                _T_40 <= _T_61 
                skip 
              else : 
                _T_40 <= _T_39 
                skip 
            skip 
          else : 
            _T_40 <= _T_39 
            skip 
          io.pipeline.readdata <= _T_40 
          skip 
      skip 
    
