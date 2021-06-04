circuit ListBuffer : 
  module ListBuffer : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip push : {flip ready : UInt<1>, valid : UInt<1>, bits : {index : UInt<6>, data : {data : UInt<64>, mask : UInt<8>, corrupt : UInt<1>}}}, valid : UInt<40>, flip pop : {valid : UInt<1>, bits : UInt<6>}, data : {data : UInt<64>, mask : UInt<8>, corrupt : UInt<1>}}
    
    clock is invalid
    reset is invalid
    io is invalid
    reg valid : UInt<40>, clock with : (reset => (reset, UInt<40>("h00"))) @[ListBuffer.scala 45:22]
    cmem head : UInt<6>[40] @[ListBuffer.scala 46:18]
    cmem tail : UInt<6>[40] @[ListBuffer.scala 47:18]
    reg used : UInt<40>, clock with : (reset => (reset, UInt<40>("h00"))) @[ListBuffer.scala 48:22]
    cmem next : UInt<6>[40] @[ListBuffer.scala 49:18]
    cmem data : {data : UInt<64>, mask : UInt<8>, corrupt : UInt<1>}[40] @[ListBuffer.scala 50:18]
    node _T = not(used) @[ListBuffer.scala 52:25]
    node _T_1 = shl(_T, 1) @[package.scala 199:48]
    node _T_2 = bits(_T_1, 39, 0) @[package.scala 199:53]
    node _T_3 = or(_T, _T_2) @[package.scala 199:43]
    node _T_4 = shl(_T_3, 2) @[package.scala 199:48]
    node _T_5 = bits(_T_4, 39, 0) @[package.scala 199:53]
    node _T_6 = or(_T_3, _T_5) @[package.scala 199:43]
    node _T_7 = shl(_T_6, 4) @[package.scala 199:48]
    node _T_8 = bits(_T_7, 39, 0) @[package.scala 199:53]
    node _T_9 = or(_T_6, _T_8) @[package.scala 199:43]
    node _T_10 = shl(_T_9, 8) @[package.scala 199:48]
    node _T_11 = bits(_T_10, 39, 0) @[package.scala 199:53]
    node _T_12 = or(_T_9, _T_11) @[package.scala 199:43]
    node _T_13 = shl(_T_12, 16) @[package.scala 199:48]
    node _T_14 = bits(_T_13, 39, 0) @[package.scala 199:53]
    node _T_15 = or(_T_12, _T_14) @[package.scala 199:43]
    node _T_16 = shl(_T_15, 32) @[package.scala 199:48]
    node _T_17 = bits(_T_16, 39, 0) @[package.scala 199:53]
    node _T_18 = or(_T_15, _T_17) @[package.scala 199:43]
    node _T_19 = bits(_T_18, 39, 0) @[package.scala 200:17]
    node _T_20 = shl(_T_19, 1) @[ListBuffer.scala 52:32]
    node _T_21 = not(_T_20) @[ListBuffer.scala 52:16]
    node _T_22 = not(used) @[ListBuffer.scala 52:40]
    node freeOH = and(_T_21, _T_22) @[ListBuffer.scala 52:38]
    node _T_23 = bits(freeOH, 40, 32) @[OneHot.scala 30:18]
    node _T_24 = bits(freeOH, 31, 0) @[OneHot.scala 31:18]
    node _T_25 = orr(_T_23) @[OneHot.scala 32:14]
    node _T_26 = or(_T_23, _T_24) @[OneHot.scala 32:28]
    node _T_27 = bits(_T_26, 31, 16) @[OneHot.scala 30:18]
    node _T_28 = bits(_T_26, 15, 0) @[OneHot.scala 31:18]
    node _T_29 = orr(_T_27) @[OneHot.scala 32:14]
    node _T_30 = or(_T_27, _T_28) @[OneHot.scala 32:28]
    node _T_31 = bits(_T_30, 15, 8) @[OneHot.scala 30:18]
    node _T_32 = bits(_T_30, 7, 0) @[OneHot.scala 31:18]
    node _T_33 = orr(_T_31) @[OneHot.scala 32:14]
    node _T_34 = or(_T_31, _T_32) @[OneHot.scala 32:28]
    node _T_35 = bits(_T_34, 7, 4) @[OneHot.scala 30:18]
    node _T_36 = bits(_T_34, 3, 0) @[OneHot.scala 31:18]
    node _T_37 = orr(_T_35) @[OneHot.scala 32:14]
    node _T_38 = or(_T_35, _T_36) @[OneHot.scala 32:28]
    node _T_39 = bits(_T_38, 3, 2) @[OneHot.scala 30:18]
    node _T_40 = bits(_T_38, 1, 0) @[OneHot.scala 31:18]
    node _T_41 = orr(_T_39) @[OneHot.scala 32:14]
    node _T_42 = or(_T_39, _T_40) @[OneHot.scala 32:28]
    node _T_43 = bits(_T_42, 1, 1) @[CircuitMath.scala 30:8]
    node _T_44 = cat(_T_41, _T_43) @[Cat.scala 29:58]
    node _T_45 = cat(_T_37, _T_44) @[Cat.scala 29:58]
    node _T_46 = cat(_T_33, _T_45) @[Cat.scala 29:58]
    node _T_47 = cat(_T_29, _T_46) @[Cat.scala 29:58]
    node freeIdx = cat(_T_25, _T_47) @[Cat.scala 29:58]
    wire valid_set : UInt<40>
    valid_set is invalid
    valid_set <= UInt<40>("h00")
    wire valid_clr : UInt<40>
    valid_clr is invalid
    valid_clr <= UInt<40>("h00")
    wire used_set : UInt<40>
    used_set is invalid
    used_set <= UInt<40>("h00")
    wire used_clr : UInt<40>
    used_clr is invalid
    used_clr <= UInt<40>("h00")
    read mport push_tail = tail[io.push.bits.index], clock @[ListBuffer.scala 60:28]
    node _T_48 = dshr(valid, io.push.bits.index) @[ListBuffer.scala 61:25]
    node push_valid = bits(_T_48, 0, 0) @[ListBuffer.scala 61:25]
    node _T_49 = andr(used) @[ListBuffer.scala 63:30]
    node _T_50 = eq(_T_49, UInt<1>("h00")) @[ListBuffer.scala 63:20]
    io.push.ready <= _T_50 @[ListBuffer.scala 63:17]
    node _T_51 = and(io.push.ready, io.push.valid) @[Decoupled.scala 40:37]
    when _T_51 : @[ListBuffer.scala 64:25]
      node _T_52 = bits(io.push.bits.index, 5, 0) @[OneHot.scala 64:49]
      node _T_53 = dshl(UInt<1>("h01"), _T_52) @[OneHot.scala 65:12]
      node _T_54 = bits(_T_53, 39, 0) @[OneHot.scala 65:27]
      valid_set <= _T_54 @[ListBuffer.scala 65:15]
      used_set <= freeOH @[ListBuffer.scala 66:14]
      write mport _T_55 = data[freeIdx], clock
      _T_55 <- io.push.bits.data
      when push_valid : @[ListBuffer.scala 68:23]
        write mport _T_56 = next[push_tail], clock
        _T_56 <= freeIdx
        skip @[ListBuffer.scala 68:23]
      else : @[ListBuffer.scala 70:18]
        write mport _T_57 = head[io.push.bits.index], clock
        _T_57 <= freeIdx
        skip @[ListBuffer.scala 70:18]
      write mport _T_58 = tail[io.push.bits.index], clock
      _T_58 <= freeIdx
      skip @[ListBuffer.scala 64:25]
    read mport pop_head = head[io.pop.bits], clock @[ListBuffer.scala 76:27]
    node _T_59 = dshr(valid, io.pop.bits) @[ListBuffer.scala 77:24]
    node pop_valid = bits(_T_59, 0, 0) @[ListBuffer.scala 77:24]
    read mport _T_60 = data[pop_head], clock @[ListBuffer.scala 80:44]
    io.data <- _T_60 @[ListBuffer.scala 80:11]
    io.valid <= valid @[ListBuffer.scala 81:12]
    node _T_61 = eq(io.pop.valid, UInt<1>("h00")) @[ListBuffer.scala 84:11]
    node _T_62 = dshr(io.valid, io.pop.bits) @[ListBuffer.scala 84:39]
    node _T_63 = bits(_T_62, 0, 0) @[ListBuffer.scala 84:39]
    node _T_64 = or(_T_61, _T_63) @[ListBuffer.scala 84:26]
    node _T_65 = bits(reset, 0, 0) @[ListBuffer.scala 84:10]
    node _T_66 = or(_T_64, _T_65) @[ListBuffer.scala 84:10]
    node _T_67 = eq(_T_66, UInt<1>("h00")) @[ListBuffer.scala 84:10]
    when _T_67 : @[ListBuffer.scala 84:10]
      printf(clock, UInt<1>(1), "Assertion failed\n    at ListBuffer.scala:84 assert (!io.pop.fire() || (io.valid)(io.pop.bits))\n") @[ListBuffer.scala 84:10]
      stop(clock, UInt<1>(1), 1) @[ListBuffer.scala 84:10]
      skip @[ListBuffer.scala 84:10]
    when io.pop.valid : @[ListBuffer.scala 86:24]
      node _T_68 = bits(pop_head, 5, 0) @[OneHot.scala 64:49]
      node _T_69 = dshl(UInt<1>("h01"), _T_68) @[OneHot.scala 65:12]
      node _T_70 = bits(_T_69, 39, 0) @[OneHot.scala 65:27]
      used_clr <= _T_70 @[ListBuffer.scala 87:14]
      read mport _T_71 = tail[io.pop.bits], clock @[ListBuffer.scala 88:33]
      node _T_72 = eq(pop_head, _T_71) @[ListBuffer.scala 88:20]
      when _T_72 : @[ListBuffer.scala 88:48]
        node _T_73 = bits(io.pop.bits, 5, 0) @[OneHot.scala 64:49]
        node _T_74 = dshl(UInt<1>("h01"), _T_73) @[OneHot.scala 65:12]
        node _T_75 = bits(_T_74, 39, 0) @[OneHot.scala 65:27]
        valid_clr <= _T_75 @[ListBuffer.scala 89:17]
        skip @[ListBuffer.scala 88:48]
      node _T_76 = and(io.push.ready, io.push.valid) @[Decoupled.scala 40:37]
      node _T_77 = and(_T_76, push_valid) @[ListBuffer.scala 91:48]
      node _T_78 = eq(push_tail, pop_head) @[ListBuffer.scala 91:75]
      node _T_79 = and(_T_77, _T_78) @[ListBuffer.scala 91:62]
      read mport _T_80 = next[pop_head], clock @[ListBuffer.scala 91:107]
      node _T_81 = mux(_T_79, freeIdx, _T_80) @[ListBuffer.scala 91:32]
      write mport _T_82 = head[io.pop.bits], clock
      _T_82 <= _T_81
      skip @[ListBuffer.scala 86:24]
    node _T_83 = eq(io.pop.valid, UInt<1>("h00")) @[ListBuffer.scala 95:33]
    node _T_84 = or(UInt<1>("h01"), _T_83) @[ListBuffer.scala 95:30]
    node _T_85 = or(_T_84, pop_valid) @[ListBuffer.scala 95:47]
    when _T_85 : @[ListBuffer.scala 95:61]
      node _T_86 = not(used_clr) @[ListBuffer.scala 96:23]
      node _T_87 = and(used, _T_86) @[ListBuffer.scala 96:21]
      node _T_88 = or(_T_87, used_set) @[ListBuffer.scala 96:35]
      used <= _T_88 @[ListBuffer.scala 96:11]
      node _T_89 = not(valid_clr) @[ListBuffer.scala 97:23]
      node _T_90 = and(valid, _T_89) @[ListBuffer.scala 97:21]
      node _T_91 = or(_T_90, valid_set) @[ListBuffer.scala 97:35]
      valid <= _T_91 @[ListBuffer.scala 97:11]
      skip @[ListBuffer.scala 95:61]
