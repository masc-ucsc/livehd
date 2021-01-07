circuit ICache :
  module ICache :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip req : { valid : UInt<1>, bits : { addr : UInt<39>}}, flip s1_paddr : UInt<32>, flip s1_kill : UInt<1>, flip s2_kill : UInt<1>, resp : { flip ready : UInt<1>, valid : UInt<1>, bits : { data : UInt<16>, datablock : UInt<64>}}, flip invalidate : UInt<1>, mem : { 0 : { a : { flip ready : UInt<1>, valid : UInt<1>, bits : { opcode : UInt<3>, param : UInt<3>, size : UInt<4>, source : UInt<1>, address : UInt<32>, mask : UInt<8>, data : UInt<64>}}, flip b : { flip ready : UInt<1>, valid : UInt<1>, bits : { opcode : UInt<3>, param : UInt<2>, size : UInt<4>, source : UInt<1>, address : UInt<32>, mask : UInt<8>, data : UInt<64>}}, c : { flip ready : UInt<1>, valid : UInt<1>, bits : { opcode : UInt<3>, param : UInt<3>, size : UInt<4>, source : UInt<1>, address : UInt<32>, data : UInt<64>, error : UInt<1>}}, flip d : { flip ready : UInt<1>, valid : UInt<1>, bits : { opcode : UInt<3>, param : UInt<2>, size : UInt<4>, source : UInt<1>, sink : UInt<4>, addr_lo : UInt<3>, data : UInt<64>, error : UInt<1>}}, e : { flip ready : UInt<1>, valid : UInt<1>, bits : { sink : UInt<4>}}}}}

    io is invalid
    io is invalid
    reg state : UInt<2>, clock with :
      reset => (reset, UInt<2>("h0")) @[ICache.scala 67:18]
    reg invalidated : UInt<1>, clock with :
      reset => (UInt<1>("h0"), invalidated) @[ICache.scala 68:24]
    node stall = eq(io.resp.ready, UInt<1>("h0")) @[ICache.scala 69:15]
    reg refill_addr : UInt<32>, clock with :
      reset => (UInt<1>("h0"), refill_addr) @[ICache.scala 71:24]
    wire s1_any_tag_hit : UInt<1> @[ICache.scala 72:28]
    s1_any_tag_hit is invalid @[ICache.scala 72:28]
    reg s1_valid : UInt<1>, clock with :
      reset => (reset, UInt<1>("h0")) @[ICache.scala 74:21]
    node _T_221 = eq(io.s1_kill, UInt<1>("h0")) @[ICache.scala 75:31]
    node _T_222 = and(s1_valid, _T_221) @[ICache.scala 75:28]
    node _T_223 = eq(state, UInt<2>("h0")) @[ICache.scala 75:52]
    node out_valid = and(_T_222, _T_223) @[ICache.scala 75:43]
    node s1_idx = bits(io.s1_paddr, 11, 6) @[ICache.scala 76:27]
    node s1_tag = bits(io.s1_paddr, 31, 12) @[ICache.scala 77:27]
    node s1_hit = and(out_valid, s1_any_tag_hit) @[ICache.scala 78:26]
    node _T_225 = eq(s1_any_tag_hit, UInt<1>("h0")) @[ICache.scala 79:30]
    node s1_miss = and(out_valid, _T_225) @[ICache.scala 79:27]
    node _T_226 = eq(state, UInt<2>("h0")) @[ICache.scala 81:40]
    node _T_227 = and(io.req.valid, _T_226) @[ICache.scala 81:31]
    node _T_228 = and(out_valid, stall) @[ICache.scala 81:67]
    node _T_230 = eq(_T_228, UInt<1>("h0")) @[ICache.scala 81:55]
    node s0_valid = and(_T_227, _T_230) @[ICache.scala 81:52]
    node _T_231 = and(out_valid, stall) @[ICache.scala 84:37]
    node _T_232 = or(s0_valid, _T_231) @[ICache.scala 84:24]
    s1_valid <= _T_232 @[ICache.scala 84:12]
    node _T_233 = eq(state, UInt<2>("h0")) @[ICache.scala 86:26]
    node _T_234 = and(s1_miss, _T_233) @[ICache.scala 86:17]
    when _T_234 : @[ICache.scala 86:39]
      refill_addr <= io.s1_paddr @[ICache.scala 87:17]
    node refill_tag = bits(refill_addr, 31, 12) @[ICache.scala 89:31]
    node refill_idx = bits(refill_addr, 11, 6) @[ICache.scala 90:31]
    node _T_235 = and(io.mem.0.d.ready, io.mem.0.d.valid) @[Decoupled.scala 30:37]
    node _T_237 = asUInt(asSInt(UInt<8>("hff"))) @[package.scala 19:64]
    node _T_238 = dshl(_T_237, io.mem.0.d.bits.size) @[package.scala 19:71]
    node _T_239 = bits(_T_238, 7, 0) @[package.scala 19:76]
    node _T_240 = not(_T_239) @[package.scala 19:40]
    node _T_241 = shr(_T_240, 3) @[Edges.scala 198:59]
    node _T_242 = bits(io.mem.0.d.bits.opcode, 0, 0) @[Edges.scala 90:36]
    node _T_244 = mux(_T_242, _T_241, UInt<1>("h0")) @[Edges.scala 199:14]
    reg _T_246 : UInt<5>, clock with :
      reset => (reset, UInt<5>("h0")) @[Reg.scala 26:44]
    node _T_248 = sub(_T_246, UInt<1>("h1")) @[Edges.scala 208:28]
    node _T_249 = asUInt(_T_248) @[Edges.scala 208:28]
    node _T_250 = tail(_T_249, 1) @[Edges.scala 208:28]
    node _T_252 = eq(_T_246, UInt<1>("h0")) @[Edges.scala 209:25]
    node _T_254 = eq(_T_246, UInt<1>("h1")) @[Edges.scala 210:25]
    node _T_256 = eq(_T_244, UInt<1>("h0")) @[Edges.scala 210:47]
    node _T_257 = or(_T_254, _T_256) @[Edges.scala 210:37]
    node refill_done = and(_T_257, _T_235) @[Edges.scala 211:22]
    node _T_258 = not(_T_250) @[Edges.scala 212:27]
    node refill_cnt = and(_T_244, _T_258) @[Edges.scala 212:25]
    when _T_235 : @[Edges.scala 213:17]
      node _T_259 = mux(_T_252, _T_244, _T_250) @[Edges.scala 214:21]
      _T_246 <= _T_259 @[Edges.scala 214:15]
    io.mem.0.d.ready <= UInt<1>("h1") @[ICache.scala 92:18]
    reg _T_262 : UInt<16>, clock with :
      reset => (reset, UInt<16>("h1")) @[LFSR.scala 22:19]
    when s1_miss : @[LFSR.scala 23:22]
      node _T_263 = bits(_T_262, 0, 0) @[LFSR.scala 23:40]
      node _T_264 = bits(_T_262, 2, 2) @[LFSR.scala 23:48]
      node _T_265 = xor(_T_263, _T_264) @[LFSR.scala 23:43]
      node _T_266 = bits(_T_262, 3, 3) @[LFSR.scala 23:56]
      node _T_267 = xor(_T_265, _T_266) @[LFSR.scala 23:51]
      node _T_268 = bits(_T_262, 5, 5) @[LFSR.scala 23:64]
      node _T_269 = xor(_T_267, _T_268) @[LFSR.scala 23:59]
      node _T_270 = bits(_T_262, 15, 1) @[LFSR.scala 23:73]
      node _T_271 = cat(_T_269, _T_270) @[Cat.scala 30:58]
      _T_262 <= _T_271 @[LFSR.scala 23:29]
    node repl_way = bits(_T_262, 1, 0) @[ICache.scala 95:56]
    mem tag_array : @[ICache.scala 97:25]
      data-type => UInt<20>[4]
      depth => 64
      read-latency => 1
      write-latency => 1
      reader => tag_rdata
      writer => _T_328
      read-under-write => undefined
    tag_array.tag_rdata.addr is invalid @[ICache.scala 97:25]
    tag_array.tag_rdata.clk is invalid @[ICache.scala 97:25]
    tag_array.tag_rdata.en <= UInt<1>("h0") @[ICache.scala 97:25]
    tag_array._T_328.addr is invalid @[ICache.scala 97:25]
    tag_array._T_328.clk is invalid @[ICache.scala 97:25]
    tag_array._T_328.en <= UInt<1>("h0") @[ICache.scala 97:25]
    tag_array._T_328.data is invalid @[ICache.scala 97:25]
    tag_array._T_328.mask is invalid @[ICache.scala 97:25]
    node _T_282 = bits(io.req.bits.addr, 11, 6) @[ICache.scala 98:42]
    node _T_284 = eq(refill_done, UInt<1>("h0")) @[ICache.scala 98:70]
    node _T_285 = and(_T_284, s0_valid) @[ICache.scala 98:83]
    wire _T_287 : UInt<6>
    _T_287 is invalid
    when _T_285 :
      _T_287 <= _T_282
      node _T_289 = or(_T_287, UInt<6>("h0"))
      node _T_290 = bits(_T_289, 5, 0)
      tag_array.tag_rdata.en <= UInt<1>("h1")
      tag_array.tag_rdata.addr <= _T_290
      tag_array.tag_rdata.clk <= clock
    when refill_done : @[ICache.scala 99:22]
      wire _T_304 : UInt<20>[4] @[ICache.scala 101:48]
      _T_304 is invalid @[ICache.scala 101:48]
      _T_304[0] <= refill_tag @[ICache.scala 101:48]
      _T_304[1] <= refill_tag @[ICache.scala 101:48]
      _T_304[2] <= refill_tag @[ICache.scala 101:48]
      _T_304[3] <= refill_tag @[ICache.scala 101:48]
      node _T_312 = eq(repl_way, UInt<1>("h0")) @[ICache.scala 101:84]
      node _T_314 = eq(repl_way, UInt<1>("h1")) @[ICache.scala 101:84]
      node _T_316 = eq(repl_way, UInt<2>("h2")) @[ICache.scala 101:84]
      node _T_318 = eq(repl_way, UInt<2>("h3")) @[ICache.scala 101:84]
      wire _T_321 : UInt<1>[4] @[ICache.scala 101:74]
      _T_321 is invalid @[ICache.scala 101:74]
      _T_321[0] <= _T_312 @[ICache.scala 101:74]
      _T_321[1] <= _T_314 @[ICache.scala 101:74]
      _T_321[2] <= _T_316 @[ICache.scala 101:74]
      _T_321[3] <= _T_318 @[ICache.scala 101:74]
      tag_array._T_328.addr <= refill_idx
      tag_array._T_328.clk <= clock
      tag_array._T_328.en <= UInt<1>("h1")
      tag_array._T_328.mask[0] <= UInt<1>("h0")
      tag_array._T_328.mask[1] <= UInt<1>("h0")
      tag_array._T_328.mask[2] <= UInt<1>("h0")
      tag_array._T_328.mask[3] <= UInt<1>("h0")
      when _T_321[0] :
        tag_array._T_328.data[0] <= _T_304[0]
        tag_array._T_328.mask[0] <= UInt<1>("h1")
      when _T_321[1] :
        tag_array._T_328.data[1] <= _T_304[1]
        tag_array._T_328.mask[1] <= UInt<1>("h1")
      when _T_321[2] :
        tag_array._T_328.data[2] <= _T_304[2]
        tag_array._T_328.mask[2] <= UInt<1>("h1")
      when _T_321[3] :
        tag_array._T_328.data[3] <= _T_304[3]
        tag_array._T_328.mask[3] <= UInt<1>("h1")
    reg vb_array : UInt<256>, clock with :
      reset => (reset, UInt<256>("h0")) @[ICache.scala 104:21]
    node _T_342 = eq(invalidated, UInt<1>("h0")) @[ICache.scala 105:24]
    node _T_343 = and(refill_done, _T_342) @[ICache.scala 105:21]
    when _T_343 : @[ICache.scala 105:38]
      node _T_344 = cat(repl_way, refill_idx) @[Cat.scala 30:58]
      node _T_347 = dshl(UInt<1>("h1"), _T_344) @[ICache.scala 106:32]
      node _T_348 = or(vb_array, _T_347) @[ICache.scala 106:32]
      node _T_349 = not(vb_array) @[ICache.scala 106:32]
      node _T_350 = or(_T_349, _T_347) @[ICache.scala 106:32]
      node _T_351 = not(_T_350) @[ICache.scala 106:32]
      node _T_352 = mux(UInt<1>("h1"), _T_348, _T_351) @[ICache.scala 106:32]
      vb_array <= _T_352 @[ICache.scala 106:14]
    when io.invalidate : @[ICache.scala 108:24]
      vb_array <= UInt<1>("h0") @[ICache.scala 109:14]
      invalidated <= UInt<1>("h1") @[ICache.scala 110:17]
    wire s1_disparity : UInt<1>[4] @[ICache.scala 112:26]
    s1_disparity is invalid @[ICache.scala 112:26]
    node _T_364 = and(s1_valid, s1_disparity[0]) @[ICache.scala 114:20]
    when _T_364 : @[ICache.scala 114:40]
      node _T_366 = cat(UInt<1>("h0"), s1_idx) @[Cat.scala 30:58]
      node _T_369 = dshl(UInt<1>("h1"), _T_366) @[ICache.scala 114:69]
      node _T_370 = or(vb_array, _T_369) @[ICache.scala 114:69]
      node _T_371 = not(vb_array) @[ICache.scala 114:69]
      node _T_372 = or(_T_371, _T_369) @[ICache.scala 114:69]
      node _T_373 = not(_T_372) @[ICache.scala 114:69]
      node _T_374 = mux(UInt<1>("h0"), _T_370, _T_373) @[ICache.scala 114:69]
      vb_array <= _T_374 @[ICache.scala 114:51]
    node _T_375 = and(s1_valid, s1_disparity[1]) @[ICache.scala 114:20]
    when _T_375 : @[ICache.scala 114:40]
      node _T_377 = cat(UInt<1>("h1"), s1_idx) @[Cat.scala 30:58]
      node _T_380 = dshl(UInt<1>("h1"), _T_377) @[ICache.scala 114:69]
      node _T_381 = or(vb_array, _T_380) @[ICache.scala 114:69]
      node _T_382 = not(vb_array) @[ICache.scala 114:69]
      node _T_383 = or(_T_382, _T_380) @[ICache.scala 114:69]
      node _T_384 = not(_T_383) @[ICache.scala 114:69]
      node _T_385 = mux(UInt<1>("h0"), _T_381, _T_384) @[ICache.scala 114:69]
      vb_array <= _T_385 @[ICache.scala 114:51]
    node _T_386 = and(s1_valid, s1_disparity[2]) @[ICache.scala 114:20]
    when _T_386 : @[ICache.scala 114:40]
      node _T_388 = cat(UInt<2>("h2"), s1_idx) @[Cat.scala 30:58]
      node _T_391 = dshl(UInt<1>("h1"), _T_388) @[ICache.scala 114:69]
      node _T_392 = or(vb_array, _T_391) @[ICache.scala 114:69]
      node _T_393 = not(vb_array) @[ICache.scala 114:69]
      node _T_394 = or(_T_393, _T_391) @[ICache.scala 114:69]
      node _T_395 = not(_T_394) @[ICache.scala 114:69]
      node _T_396 = mux(UInt<1>("h0"), _T_392, _T_395) @[ICache.scala 114:69]
      vb_array <= _T_396 @[ICache.scala 114:51]
    node _T_397 = and(s1_valid, s1_disparity[3]) @[ICache.scala 114:20]
    when _T_397 : @[ICache.scala 114:40]
      node _T_399 = cat(UInt<2>("h3"), s1_idx) @[Cat.scala 30:58]
      node _T_402 = dshl(UInt<1>("h1"), _T_399) @[ICache.scala 114:69]
      node _T_403 = or(vb_array, _T_402) @[ICache.scala 114:69]
      node _T_404 = not(vb_array) @[ICache.scala 114:69]
      node _T_405 = or(_T_404, _T_402) @[ICache.scala 114:69]
      node _T_406 = not(_T_405) @[ICache.scala 114:69]
      node _T_407 = mux(UInt<1>("h0"), _T_403, _T_406) @[ICache.scala 114:69]
      vb_array <= _T_407 @[ICache.scala 114:51]
    wire s1_tag_match : UInt<1>[4] @[ICache.scala 116:26]
    s1_tag_match is invalid @[ICache.scala 116:26]
    wire s1_tag_hit : UInt<1>[4] @[ICache.scala 117:24]
    s1_tag_hit is invalid @[ICache.scala 117:24]
    wire s1_dout : UInt<64>[4] @[ICache.scala 118:21]
    s1_dout is invalid @[ICache.scala 118:21]
    reg s1_dout_valid : UInt<1>, clock with :
      reset => (UInt<1>("h0"), s1_dout_valid) @[Reg.scala 14:44]
    s1_dout_valid <= s0_valid @[Reg.scala 14:44]
    node _T_436 = eq(io.invalidate, UInt<1>("h0")) @[ICache.scala 122:17]
    node _T_438 = bits(io.s1_paddr, 11, 6) @[ICache.scala 122:68]
    node _T_439 = cat(UInt<1>("h0"), _T_438) @[Cat.scala 30:58]
    node _T_440 = dshr(vb_array, _T_439) @[ICache.scala 122:43]
    node _T_441 = bits(_T_440, 0, 0) @[ICache.scala 122:43]
    node _T_442 = bits(_T_441, 0, 0) @[ICache.scala 122:97]
    node _T_443 = and(_T_436, _T_442) @[ICache.scala 122:32]
    node _T_446 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    reg _T_448 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_448) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_448 <= _T_446 @[Reg.scala 35:23]
    node _T_449 = mux(s1_dout_valid, _T_446, _T_448) @[Package.scala 27:42]
    node _T_450 = bits(tag_array.tag_rdata.data[0], 19, 0) @[ICache.scala 125:32]
    node _T_451 = eq(_T_450, s1_tag) @[ICache.scala 125:46]
    reg _T_453 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_453) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_453 <= _T_451 @[Reg.scala 35:23]
    node _T_454 = mux(s1_dout_valid, _T_451, _T_453) @[Package.scala 27:42]
    s1_tag_match[0] <= _T_454 @[ICache.scala 125:21]
    node _T_455 = and(_T_443, s1_tag_match[0]) @[ICache.scala 126:28]
    s1_tag_hit[0] <= _T_455 @[ICache.scala 126:19]
    node _T_458 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    node _T_459 = or(_T_449, _T_458) @[ICache.scala 127:51]
    node _T_460 = and(_T_443, _T_459) @[ICache.scala 127:30]
    s1_disparity[0] <= _T_460 @[ICache.scala 127:21]
    node _T_462 = eq(io.invalidate, UInt<1>("h0")) @[ICache.scala 122:17]
    node _T_464 = bits(io.s1_paddr, 11, 6) @[ICache.scala 122:68]
    node _T_465 = cat(UInt<1>("h1"), _T_464) @[Cat.scala 30:58]
    node _T_466 = dshr(vb_array, _T_465) @[ICache.scala 122:43]
    node _T_467 = bits(_T_466, 0, 0) @[ICache.scala 122:43]
    node _T_468 = bits(_T_467, 0, 0) @[ICache.scala 122:97]
    node _T_469 = and(_T_462, _T_468) @[ICache.scala 122:32]
    node _T_472 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    reg _T_474 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_474) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_474 <= _T_472 @[Reg.scala 35:23]
    node _T_475 = mux(s1_dout_valid, _T_472, _T_474) @[Package.scala 27:42]
    node _T_476 = bits(tag_array.tag_rdata.data[1], 19, 0) @[ICache.scala 125:32]
    node _T_477 = eq(_T_476, s1_tag) @[ICache.scala 125:46]
    reg _T_479 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_479) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_479 <= _T_477 @[Reg.scala 35:23]
    node _T_480 = mux(s1_dout_valid, _T_477, _T_479) @[Package.scala 27:42]
    s1_tag_match[1] <= _T_480 @[ICache.scala 125:21]
    node _T_481 = and(_T_469, s1_tag_match[1]) @[ICache.scala 126:28]
    s1_tag_hit[1] <= _T_481 @[ICache.scala 126:19]
    node _T_484 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    node _T_485 = or(_T_475, _T_484) @[ICache.scala 127:51]
    node _T_486 = and(_T_469, _T_485) @[ICache.scala 127:30]
    s1_disparity[1] <= _T_486 @[ICache.scala 127:21]
    node _T_488 = eq(io.invalidate, UInt<1>("h0")) @[ICache.scala 122:17]
    node _T_490 = bits(io.s1_paddr, 11, 6) @[ICache.scala 122:68]
    node _T_491 = cat(UInt<2>("h2"), _T_490) @[Cat.scala 30:58]
    node _T_492 = dshr(vb_array, _T_491) @[ICache.scala 122:43]
    node _T_493 = bits(_T_492, 0, 0) @[ICache.scala 122:43]
    node _T_494 = bits(_T_493, 0, 0) @[ICache.scala 122:97]
    node _T_495 = and(_T_488, _T_494) @[ICache.scala 122:32]
    node _T_498 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    reg _T_500 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_500) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_500 <= _T_498 @[Reg.scala 35:23]
    node _T_501 = mux(s1_dout_valid, _T_498, _T_500) @[Package.scala 27:42]
    node _T_502 = bits(tag_array.tag_rdata.data[2], 19, 0) @[ICache.scala 125:32]
    node _T_503 = eq(_T_502, s1_tag) @[ICache.scala 125:46]
    reg _T_505 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_505) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_505 <= _T_503 @[Reg.scala 35:23]
    node _T_506 = mux(s1_dout_valid, _T_503, _T_505) @[Package.scala 27:42]
    s1_tag_match[2] <= _T_506 @[ICache.scala 125:21]
    node _T_507 = and(_T_495, s1_tag_match[2]) @[ICache.scala 126:28]
    s1_tag_hit[2] <= _T_507 @[ICache.scala 126:19]
    node _T_510 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    node _T_511 = or(_T_501, _T_510) @[ICache.scala 127:51]
    node _T_512 = and(_T_495, _T_511) @[ICache.scala 127:30]
    s1_disparity[2] <= _T_512 @[ICache.scala 127:21]
    node _T_514 = eq(io.invalidate, UInt<1>("h0")) @[ICache.scala 122:17]
    node _T_516 = bits(io.s1_paddr, 11, 6) @[ICache.scala 122:68]
    node _T_517 = cat(UInt<2>("h3"), _T_516) @[Cat.scala 30:58]
    node _T_518 = dshr(vb_array, _T_517) @[ICache.scala 122:43]
    node _T_519 = bits(_T_518, 0, 0) @[ICache.scala 122:43]
    node _T_520 = bits(_T_519, 0, 0) @[ICache.scala 122:97]
    node _T_521 = and(_T_514, _T_520) @[ICache.scala 122:32]
    node _T_524 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    reg _T_526 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_526) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_526 <= _T_524 @[Reg.scala 35:23]
    node _T_527 = mux(s1_dout_valid, _T_524, _T_526) @[Package.scala 27:42]
    node _T_528 = bits(tag_array.tag_rdata.data[3], 19, 0) @[ICache.scala 125:32]
    node _T_529 = eq(_T_528, s1_tag) @[ICache.scala 125:46]
    reg _T_531 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), _T_531) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_531 <= _T_529 @[Reg.scala 35:23]
    node _T_532 = mux(s1_dout_valid, _T_529, _T_531) @[Package.scala 27:42]
    s1_tag_match[3] <= _T_532 @[ICache.scala 125:21]
    node _T_533 = and(_T_521, s1_tag_match[3]) @[ICache.scala 126:28]
    s1_tag_hit[3] <= _T_533 @[ICache.scala 126:19]
    node _T_536 = or(UInt<1>("h0"), UInt<1>("h0")) @[Ecc.scala 14:27]
    node _T_537 = or(_T_527, _T_536) @[ICache.scala 127:51]
    node _T_538 = and(_T_521, _T_537) @[ICache.scala 127:30]
    s1_disparity[3] <= _T_538 @[ICache.scala 127:21]
    node _T_539 = or(s1_tag_hit[0], s1_tag_hit[1]) @[ICache.scala 129:44]
    node _T_540 = or(_T_539, s1_tag_hit[2]) @[ICache.scala 129:44]
    node _T_541 = or(_T_540, s1_tag_hit[3]) @[ICache.scala 129:44]
    node _T_542 = or(s1_disparity[0], s1_disparity[1]) @[ICache.scala 129:78]
    node _T_543 = or(_T_542, s1_disparity[2]) @[ICache.scala 129:78]
    node _T_544 = or(_T_543, s1_disparity[3]) @[ICache.scala 129:78]
    node _T_546 = eq(_T_544, UInt<1>("h0")) @[ICache.scala 129:52]
    node _T_547 = and(_T_541, _T_546) @[ICache.scala 129:49]
    s1_any_tag_hit <= _T_547 @[ICache.scala 129:18]
    mem _T_550 : @[ICache.scala 132:28]
      data-type => UInt<64>
      depth => 512
      read-latency => 1
      write-latency => 1
      reader => _T_566
      writer => _T_556
      read-under-write => undefined
    _T_550._T_566.addr is invalid @[ICache.scala 132:28]
    _T_550._T_566.clk is invalid @[ICache.scala 132:28]
    _T_550._T_566.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_550._T_556.addr is invalid @[ICache.scala 132:28]
    _T_550._T_556.clk is invalid @[ICache.scala 132:28]
    _T_550._T_556.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_550._T_556.data is invalid @[ICache.scala 132:28]
    _T_550._T_556.mask is invalid @[ICache.scala 132:28]
    node _T_552 = eq(repl_way, UInt<1>("h0")) @[ICache.scala 133:42]
    node _T_553 = and(io.mem.0.d.valid, _T_552) @[ICache.scala 133:30]
    when _T_553 : @[ICache.scala 134:16]
      node _T_554 = shl(refill_idx, 3) @[ICache.scala 136:36]
      node _T_555 = or(_T_554, refill_cnt) @[ICache.scala 136:63]
      _T_550._T_556.addr <= _T_555
      _T_550._T_556.clk <= clock
      _T_550._T_556.en <= UInt<1>("h1")
      _T_550._T_556.mask <= UInt<1>("h0")
      _T_550._T_556.data <= io.mem.0.d.bits.data
      _T_550._T_556.mask <= UInt<1>("h1")
    node _T_557 = bits(io.req.bits.addr, 11, 3) @[ICache.scala 138:28]
    node _T_559 = eq(_T_553, UInt<1>("h0")) @[ICache.scala 139:45]
    node _T_560 = and(_T_559, s0_valid) @[ICache.scala 139:50]
    wire _T_562 : UInt<9>
    _T_562 is invalid
    when _T_560 :
      _T_562 <= _T_557
      node _T_564 = or(_T_562, UInt<9>("h0"))
      node _T_565 = bits(_T_564, 8, 0)
      _T_550._T_566.en <= UInt<1>("h1")
      _T_550._T_566.addr <= _T_565
      _T_550._T_566.clk <= clock
    reg _T_568 : UInt<64>, clock with :
      reset => (UInt<1>("h0"), _T_568) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_568 <= _T_550._T_566.data @[Reg.scala 35:23]
    node _T_569 = mux(s1_dout_valid, _T_550._T_566.data, _T_568) @[Package.scala 27:42]
    s1_dout[0] <= _T_569 @[ICache.scala 139:16]
    mem _T_572 : @[ICache.scala 132:28]
      data-type => UInt<64>
      depth => 512
      read-latency => 1
      write-latency => 1
      reader => _T_588
      writer => _T_578
      read-under-write => undefined
    _T_572._T_588.addr is invalid @[ICache.scala 132:28]
    _T_572._T_588.clk is invalid @[ICache.scala 132:28]
    _T_572._T_588.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_572._T_578.addr is invalid @[ICache.scala 132:28]
    _T_572._T_578.clk is invalid @[ICache.scala 132:28]
    _T_572._T_578.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_572._T_578.data is invalid @[ICache.scala 132:28]
    _T_572._T_578.mask is invalid @[ICache.scala 132:28]
    node _T_574 = eq(repl_way, UInt<1>("h1")) @[ICache.scala 133:42]
    node _T_575 = and(io.mem.0.d.valid, _T_574) @[ICache.scala 133:30]
    when _T_575 : @[ICache.scala 134:16]
      node _T_576 = shl(refill_idx, 3) @[ICache.scala 136:36]
      node _T_577 = or(_T_576, refill_cnt) @[ICache.scala 136:63]
      _T_572._T_578.addr <= _T_577
      _T_572._T_578.clk <= clock
      _T_572._T_578.en <= UInt<1>("h1")
      _T_572._T_578.mask <= UInt<1>("h0")
      _T_572._T_578.data <= io.mem.0.d.bits.data
      _T_572._T_578.mask <= UInt<1>("h1")
    node _T_579 = bits(io.req.bits.addr, 11, 3) @[ICache.scala 138:28]
    node _T_581 = eq(_T_575, UInt<1>("h0")) @[ICache.scala 139:45]
    node _T_582 = and(_T_581, s0_valid) @[ICache.scala 139:50]
    wire _T_584 : UInt<9>
    _T_584 is invalid
    when _T_582 :
      _T_584 <= _T_579
      node _T_586 = or(_T_584, UInt<9>("h0"))
      node _T_587 = bits(_T_586, 8, 0)
      _T_572._T_588.en <= UInt<1>("h1")
      _T_572._T_588.addr <= _T_587
      _T_572._T_588.clk <= clock
    reg _T_590 : UInt<64>, clock with :
      reset => (UInt<1>("h0"), _T_590) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_590 <= _T_572._T_588.data @[Reg.scala 35:23]
    node _T_591 = mux(s1_dout_valid, _T_572._T_588.data, _T_590) @[Package.scala 27:42]
    s1_dout[1] <= _T_591 @[ICache.scala 139:16]
    mem _T_594 : @[ICache.scala 132:28]
      data-type => UInt<64>
      depth => 512
      read-latency => 1
      write-latency => 1
      reader => _T_610
      writer => _T_600
      read-under-write => undefined
    _T_594._T_610.addr is invalid @[ICache.scala 132:28]
    _T_594._T_610.clk is invalid @[ICache.scala 132:28]
    _T_594._T_610.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_594._T_600.addr is invalid @[ICache.scala 132:28]
    _T_594._T_600.clk is invalid @[ICache.scala 132:28]
    _T_594._T_600.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_594._T_600.data is invalid @[ICache.scala 132:28]
    _T_594._T_600.mask is invalid @[ICache.scala 132:28]
    node _T_596 = eq(repl_way, UInt<2>("h2")) @[ICache.scala 133:42]
    node _T_597 = and(io.mem.0.d.valid, _T_596) @[ICache.scala 133:30]
    when _T_597 : @[ICache.scala 134:16]
      node _T_598 = shl(refill_idx, 3) @[ICache.scala 136:36]
      node _T_599 = or(_T_598, refill_cnt) @[ICache.scala 136:63]
      _T_594._T_600.addr <= _T_599
      _T_594._T_600.clk <= clock
      _T_594._T_600.en <= UInt<1>("h1")
      _T_594._T_600.mask <= UInt<1>("h0")
      _T_594._T_600.data <= io.mem.0.d.bits.data
      _T_594._T_600.mask <= UInt<1>("h1")
    node _T_601 = bits(io.req.bits.addr, 11, 3) @[ICache.scala 138:28]
    node _T_603 = eq(_T_597, UInt<1>("h0")) @[ICache.scala 139:45]
    node _T_604 = and(_T_603, s0_valid) @[ICache.scala 139:50]
    wire _T_606 : UInt<9>
    _T_606 is invalid
    when _T_604 :
      _T_606 <= _T_601
      node _T_608 = or(_T_606, UInt<9>("h0"))
      node _T_609 = bits(_T_608, 8, 0)
      _T_594._T_610.en <= UInt<1>("h1")
      _T_594._T_610.addr <= _T_609
      _T_594._T_610.clk <= clock
    reg _T_612 : UInt<64>, clock with :
      reset => (UInt<1>("h0"), _T_612) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_612 <= _T_594._T_610.data @[Reg.scala 35:23]
    node _T_613 = mux(s1_dout_valid, _T_594._T_610.data, _T_612) @[Package.scala 27:42]
    s1_dout[2] <= _T_613 @[ICache.scala 139:16]
    mem _T_616 : @[ICache.scala 132:28]
      data-type => UInt<64>
      depth => 512
      read-latency => 1
      write-latency => 1
      reader => _T_632
      writer => _T_622
      read-under-write => undefined
    _T_616._T_632.addr is invalid @[ICache.scala 132:28]
    _T_616._T_632.clk is invalid @[ICache.scala 132:28]
    _T_616._T_632.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_616._T_622.addr is invalid @[ICache.scala 132:28]
    _T_616._T_622.clk is invalid @[ICache.scala 132:28]
    _T_616._T_622.en <= UInt<1>("h0") @[ICache.scala 132:28]
    _T_616._T_622.data is invalid @[ICache.scala 132:28]
    _T_616._T_622.mask is invalid @[ICache.scala 132:28]
    node _T_618 = eq(repl_way, UInt<2>("h3")) @[ICache.scala 133:42]
    node _T_619 = and(io.mem.0.d.valid, _T_618) @[ICache.scala 133:30]
    when _T_619 : @[ICache.scala 134:16]
      node _T_620 = shl(refill_idx, 3) @[ICache.scala 136:36]
      node _T_621 = or(_T_620, refill_cnt) @[ICache.scala 136:63]
      _T_616._T_622.addr <= _T_621
      _T_616._T_622.clk <= clock
      _T_616._T_622.en <= UInt<1>("h1")
      _T_616._T_622.mask <= UInt<1>("h0")
      _T_616._T_622.data <= io.mem.0.d.bits.data
      _T_616._T_622.mask <= UInt<1>("h1")
    node _T_623 = bits(io.req.bits.addr, 11, 3) @[ICache.scala 138:28]
    node _T_625 = eq(_T_619, UInt<1>("h0")) @[ICache.scala 139:45]
    node _T_626 = and(_T_625, s0_valid) @[ICache.scala 139:50]
    wire _T_628 : UInt<9>
    _T_628 is invalid
    when _T_626 :
      _T_628 <= _T_623
      node _T_630 = or(_T_628, UInt<9>("h0"))
      node _T_631 = bits(_T_630, 8, 0)
      _T_616._T_632.en <= UInt<1>("h1")
      _T_616._T_632.addr <= _T_631
      _T_616._T_632.clk <= clock
    reg _T_634 : UInt<64>, clock with :
      reset => (UInt<1>("h0"), _T_634) @[Reg.scala 34:16]
    when s1_dout_valid : @[Reg.scala 35:19]
      _T_634 <= _T_616._T_632.data @[Reg.scala 35:23]
    node _T_635 = mux(s1_dout_valid, _T_616._T_632.data, _T_634) @[Package.scala 27:42]
    s1_dout[3] <= _T_635 @[ICache.scala 139:16]
    node _T_638 = eq(stall, UInt<1>("h0")) @[ICache.scala 148:51]
    reg _T_639 : UInt<1>, clock with :
      reset => (reset, UInt<1>("h0")) @[Reg.scala 26:44]
    when _T_638 : @[Reg.scala 43:19]
      _T_639 <= s1_hit @[Reg.scala 43:23]
    node _T_641 = eq(stall, UInt<1>("h0")) @[ICache.scala 149:46]
    reg _T_654 : UInt<1>[4], clock with :
      reset => (UInt<1>("h0"), _T_654) @[Reg.scala 34:16]
    when _T_641 : @[Reg.scala 35:19]
      _T_654 <- s1_tag_hit @[Reg.scala 35:23]
    node _T_672 = eq(stall, UInt<1>("h0")) @[ICache.scala 150:40]
    reg _T_685 : UInt<64>[4], clock with :
      reset => (UInt<1>("h0"), _T_685) @[Reg.scala 34:16]
    when _T_672 : @[Reg.scala 35:19]
      _T_685 <- s1_dout @[Reg.scala 35:23]
    node _T_703 = mux(_T_654[0], _T_685[0], UInt<1>("h0")) @[Mux.scala 19:72]
    node _T_705 = mux(_T_654[1], _T_685[1], UInt<1>("h0")) @[Mux.scala 19:72]
    node _T_707 = mux(_T_654[2], _T_685[2], UInt<1>("h0")) @[Mux.scala 19:72]
    node _T_709 = mux(_T_654[3], _T_685[3], UInt<1>("h0")) @[Mux.scala 19:72]
    node _T_711 = or(_T_703, _T_705) @[Mux.scala 19:72]
    node _T_712 = or(_T_711, _T_707) @[Mux.scala 19:72]
    node _T_713 = or(_T_712, _T_709) @[Mux.scala 19:72]
    wire _T_715 : UInt<64> @[Mux.scala 19:72]
    _T_715 is invalid @[Mux.scala 19:72]
    _T_715 <= _T_713 @[Mux.scala 19:72]
    io.resp.bits.datablock <= _T_715 @[ICache.scala 151:30]
    io.resp.valid <= _T_639 @[ICache.scala 152:21]
    node _T_716 = eq(state, UInt<2>("h1")) @[ICache.scala 154:27]
    node _T_718 = eq(io.s2_kill, UInt<1>("h0")) @[ICache.scala 154:44]
    node _T_719 = and(_T_716, _T_718) @[ICache.scala 154:41]
    io.mem.0.a.valid <= _T_719 @[ICache.scala 154:18]
    node _T_721 = shr(refill_addr, 6) @[ICache.scala 157:46]
    node _T_722 = shl(_T_721, 6) @[ICache.scala 157:63]
    node _T_726 = leq(UInt<1>("h0"), UInt<3>("h6")) @[Parameters.scala 63:32]
    node _T_728 = leq(UInt<3>("h6"), UInt<3>("h6")) @[Parameters.scala 63:42]
    node _T_729 = and(_T_726, _T_728) @[Parameters.scala 63:37]
    node _T_730 = or(UInt<1>("h0"), _T_729) @[Parameters.scala 132:31]
    node _T_732 = xor(_T_722, UInt<1>("h0")) @[Parameters.scala 117:31]
    node _T_733 = cvt(_T_732) @[Parameters.scala 117:49]
    node _T_735 = and(_T_733, asSInt(UInt<31>("h20000000"))) @[Parameters.scala 117:52]
    node _T_736 = asSInt(_T_735) @[Parameters.scala 117:52]
    node _T_738 = eq(_T_736, asSInt(UInt<1>("h0"))) @[Parameters.scala 117:67]
    node _T_739 = and(_T_730, _T_738) @[Parameters.scala 132:56]
    node _T_742 = leq(UInt<1>("h0"), UInt<3>("h6")) @[Parameters.scala 63:32]
    node _T_744 = leq(UInt<3>("h6"), UInt<4>("h8")) @[Parameters.scala 63:42]
    node _T_745 = and(_T_742, _T_744) @[Parameters.scala 63:37]
    node _T_746 = or(UInt<1>("h0"), _T_745) @[Parameters.scala 132:31]
    node _T_748 = xor(_T_722, UInt<30>("h20000000")) @[Parameters.scala 117:31]
    node _T_749 = cvt(_T_748) @[Parameters.scala 117:49]
    node _T_751 = and(_T_749, asSInt(UInt<31>("h20000000"))) @[Parameters.scala 117:52]
    node _T_752 = asSInt(_T_751) @[Parameters.scala 117:52]
    node _T_754 = eq(_T_752, asSInt(UInt<1>("h0"))) @[Parameters.scala 117:67]
    node _T_755 = and(_T_746, _T_754) @[Parameters.scala 132:56]
    node _T_757 = or(UInt<1>("h0"), _T_739) @[Parameters.scala 134:30]
    node _T_758 = or(_T_757, _T_755) @[Parameters.scala 134:30]
    wire _T_767 : { opcode : UInt<3>, param : UInt<3>, size : UInt<4>, source : UInt<1>, address : UInt<32>, mask : UInt<8>, data : UInt<64>} @[Edges.scala 342:17]
    _T_767 is invalid @[Edges.scala 342:17]
    _T_767.opcode <= UInt<3>("h4") @[Edges.scala 343:15]
    _T_767.param <= UInt<1>("h0") @[Edges.scala 344:15]
    _T_767.size <= UInt<3>("h6") @[Edges.scala 345:15]
    _T_767.source <= UInt<1>("h0") @[Edges.scala 346:15]
    _T_767.address <= _T_722 @[Edges.scala 347:15]
    node _T_779 = dshl(UInt<1>("h1"), UInt<2>("h2")) @[OneHot.scala 49:12]
    node _T_780 = bits(_T_779, 2, 0) @[OneHot.scala 49:37]
    node _T_782 = geq(UInt<3>("h6"), UInt<2>("h3")) @[package.scala 41:21]
    node _T_784 = bits(_T_780, 2, 2) @[package.scala 44:26]
    node _T_785 = bits(_T_722, 2, 2) @[package.scala 45:26]
    node _T_787 = eq(_T_785, UInt<1>("h0")) @[package.scala 46:20]
    node _T_788 = and(UInt<1>("h1"), _T_787) @[package.scala 49:27]
    node _T_789 = and(_T_784, _T_788) @[package.scala 50:38]
    node _T_790 = or(_T_782, _T_789) @[package.scala 50:29]
    node _T_791 = and(UInt<1>("h1"), _T_785) @[package.scala 49:27]
    node _T_792 = and(_T_784, _T_791) @[package.scala 50:38]
    node _T_793 = or(_T_782, _T_792) @[package.scala 50:29]
    node _T_794 = bits(_T_780, 1, 1) @[package.scala 44:26]
    node _T_795 = bits(_T_722, 1, 1) @[package.scala 45:26]
    node _T_797 = eq(_T_795, UInt<1>("h0")) @[package.scala 46:20]
    node _T_798 = and(_T_788, _T_797) @[package.scala 49:27]
    node _T_799 = and(_T_794, _T_798) @[package.scala 50:38]
    node _T_800 = or(_T_790, _T_799) @[package.scala 50:29]
    node _T_801 = and(_T_788, _T_795) @[package.scala 49:27]
    node _T_802 = and(_T_794, _T_801) @[package.scala 50:38]
    node _T_803 = or(_T_790, _T_802) @[package.scala 50:29]
    node _T_804 = and(_T_791, _T_797) @[package.scala 49:27]
    node _T_805 = and(_T_794, _T_804) @[package.scala 50:38]
    node _T_806 = or(_T_793, _T_805) @[package.scala 50:29]
    node _T_807 = and(_T_791, _T_795) @[package.scala 49:27]
    node _T_808 = and(_T_794, _T_807) @[package.scala 50:38]
    node _T_809 = or(_T_793, _T_808) @[package.scala 50:29]
    node _T_810 = bits(_T_780, 0, 0) @[package.scala 44:26]
    node _T_811 = bits(_T_722, 0, 0) @[package.scala 45:26]
    node _T_813 = eq(_T_811, UInt<1>("h0")) @[package.scala 46:20]
    node _T_814 = and(_T_798, _T_813) @[package.scala 49:27]
    node _T_815 = and(_T_810, _T_814) @[package.scala 50:38]
    node _T_816 = or(_T_800, _T_815) @[package.scala 50:29]
    node _T_817 = and(_T_798, _T_811) @[package.scala 49:27]
    node _T_818 = and(_T_810, _T_817) @[package.scala 50:38]
    node _T_819 = or(_T_800, _T_818) @[package.scala 50:29]
    node _T_820 = and(_T_801, _T_813) @[package.scala 49:27]
    node _T_821 = and(_T_810, _T_820) @[package.scala 50:38]
    node _T_822 = or(_T_803, _T_821) @[package.scala 50:29]
    node _T_823 = and(_T_801, _T_811) @[package.scala 49:27]
    node _T_824 = and(_T_810, _T_823) @[package.scala 50:38]
    node _T_825 = or(_T_803, _T_824) @[package.scala 50:29]
    node _T_826 = and(_T_804, _T_813) @[package.scala 49:27]
    node _T_827 = and(_T_810, _T_826) @[package.scala 50:38]
    node _T_828 = or(_T_806, _T_827) @[package.scala 50:29]
    node _T_829 = and(_T_804, _T_811) @[package.scala 49:27]
    node _T_830 = and(_T_810, _T_829) @[package.scala 50:38]
    node _T_831 = or(_T_806, _T_830) @[package.scala 50:29]
    node _T_832 = and(_T_807, _T_813) @[package.scala 49:27]
    node _T_833 = and(_T_810, _T_832) @[package.scala 50:38]
    node _T_834 = or(_T_809, _T_833) @[package.scala 50:29]
    node _T_835 = and(_T_807, _T_811) @[package.scala 49:27]
    node _T_836 = and(_T_810, _T_835) @[package.scala 50:38]
    node _T_837 = or(_T_809, _T_836) @[package.scala 50:29]
    node _T_838 = cat(_T_819, _T_816) @[Cat.scala 30:58]
    node _T_839 = cat(_T_825, _T_822) @[Cat.scala 30:58]
    node _T_840 = cat(_T_839, _T_838) @[Cat.scala 30:58]
    node _T_841 = cat(_T_831, _T_828) @[Cat.scala 30:58]
    node _T_842 = cat(_T_837, _T_834) @[Cat.scala 30:58]
    node _T_843 = cat(_T_842, _T_841) @[Cat.scala 30:58]
    node _T_844 = cat(_T_843, _T_840) @[Cat.scala 30:58]
    _T_767.mask <= _T_844 @[Edges.scala 348:15]
    _T_767.data <= UInt<1>("h0") @[Edges.scala 349:15]
    io.mem.0.a.bits <- _T_767 @[ICache.scala 155:17]
    io.mem.0.c.valid <= UInt<1>("h0") @[ICache.scala 159:18]
    io.mem.0.e.valid <= UInt<1>("h0") @[ICache.scala 160:18]
    node _T_848 = eq(UInt<2>("h0"), state) @[Conditional.scala 29:28]
    when _T_848 : @[Conditional.scala 29:59]
      when s1_miss : @[ICache.scala 165:22]
        state <= UInt<2>("h1") @[ICache.scala 165:30]
      invalidated <= UInt<1>("h0") @[ICache.scala 166:19]
    node _T_850 = eq(UInt<2>("h1"), state) @[Conditional.scala 29:28]
    when _T_850 : @[Conditional.scala 29:59]
      when io.mem.0.a.ready : @[ICache.scala 169:29]
        state <= UInt<2>("h2") @[ICache.scala 169:37]
      when io.s2_kill : @[ICache.scala 170:25]
        state <= UInt<2>("h0") @[ICache.scala 170:33]
    node _T_851 = eq(UInt<2>("h2"), state) @[Conditional.scala 29:28]
    when _T_851 : @[Conditional.scala 29:59]
      when io.mem.0.d.valid : @[ICache.scala 173:29]
        state <= UInt<2>("h3") @[ICache.scala 173:37]
    node _T_852 = eq(UInt<2>("h3"), state) @[Conditional.scala 29:28]
    when _T_852 : @[Conditional.scala 29:59]
      when refill_done : @[ICache.scala 176:26]
        state <= UInt<2>("h0") @[ICache.scala 176:34]