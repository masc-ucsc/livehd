const Lconst = require('./jops');

// https://www.rapidtables.com/convert/number/hex-to-decimal.html: I USED THIS WEBISTE FOR TESTING: https://www.rapidtables.com/convert/number/hex-to-decimal.html

// test, when the input is in hex
test('1. TEST from_pyrope, underscore', () => {
  let testing = Lconst.from_pyrope('0x__ab_c_');
  expect(String(testing.num)).toBe('2748');
});

test('2. TEST from_pyrope, + and - sign', () => {
  let hex_postiveWithOutSign = Lconst.from_pyrope('0xFE_E_B');
  let hex_postiveWithSign = Lconst.from_pyrope('+0x_FEEB_');

  expect(hex_postiveWithOutSign.num).toBe(65259n);
  expect(hex_postiveWithSign.num).toBe(65259n);

  // !!!!! here I need to confirm on thing: as we know, in pyrope, hex value is always unsigned, so it is impossible to have -0x_FEEB as take in value
  expect(() => {
    Lconst.from_pyrope('-0x_FEEB');
  }).toThrow();
});

test('3. TEST from_pyrope & to_pyrope, a trival test', () => {
  let testing = Lconst.from_pyrope('0x1');
  let testing2 = Lconst.from_pyrope('0x2');
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe('0x3');
});

test('4. TEST from_pyrope & to_pyrope, a average test', () => {
  let testing = Lconst.from_pyrope(
    '0x12352353464564234526246__345723564756345'
  );
  let testing2 = Lconst.from_pyrope(
    '0x2435234655463_457_6543__2545324564136161346'
  );
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe(
    '0x251671736122621551114703061247452637003'
  );
});

// test, when the input is in signed binary
// !!!!! here, notice that we have two values: the unknown and binary !!!!!
test('5. TEST when the input is a signed binary', () => {
  const testing1 = Lconst.from_pyrope('0sb1110??1');
  const testing2 = Lconst.from_pyrope('0sb0110??1');
  const testing3 = Lconst.from_pyrope('-0b1110??1');

  expect(testing1.num).toBe(-15n);
  expect(testing1.unknown).toBe(6n);
  expect(testing1.bits).toBe(5n); // notice that the extra '1' is not needed

  expect(testing2.num).toBe(49n);
  expect(testing2.unknown).toBe(6n);
  expect(testing2.bits).toBe(7n);

  // !!!! not sure; wheter this result is correct or not
  expect(testing3.num).toBe(-113n);
  expect(testing3.unknown).toBe(6n);
  expect(testing3.bits).toBe(8n);
});

test('6. TEST when the input is an unsigned binary', () => {
  const testing1 = Lconst.from_pyrope('0b110001');
  expect(testing1.num).toBe(49n);
  expect(testing1.bits).toBe(7n); // extra non-explict bit '0' is required for unsigned string
});

test('7. TESt only binary number could be signed', () => {
  expect(() => {
    Lconst.from_pyrope('0sd12345');
  }).toThrow();

  expect(() => {
    Lconst.from_pyrope('0sxFEEB');
  }).toThrow();
});

// test, when the input is in decimal
test('8. TEST when the input is decimal', () => {
  const testing = Lconst.from_pyrope(
    '0d12344325431_32312432435436546345_1234235'
  );
  const testing2 = Lconst.from_pyrope(
    '12344325431_32312432435436546345_1234235'
  );
  const testing3 = Lconst.from_pyrope(
    '0d12344325431_32312432435436546345_1234235'
  );

  // !!!! need to have attention on this one: testing4 is signed because it does not start with '0'
  const testing4 = Lconst.from_pyrope(
    '-12344325431_32312432435436546345_1234235'
  );
  expect(() => {
    Lconst.from_pyrope('-0d12344325431_32312432435436546345_1234235');
  }).toThrow();

  expect(testing.num).toBe(12344325431323124324354365463451234235n);
  expect(testing2.num).toBe(12344325431323124324354365463451234235n);
  expect(testing3.num).toBe(12344325431323124324354365463451234235n);
  expect(testing4.num).toBe(-12344325431323124324354365463451234235n);
});

// test, then the input is oct
test('9. TEST when the input is signed oct', () => {
  const testing1 = Lconst.from_pyrope('0777_777_777777777');
  const testing2 = Lconst.from_pyrope('0o777_777_777777777');
  expect(testing1.num).toBe(35184372088831n);
  expect(testing1.num).toBe(testing2.num);
});

// test, when the input is 'true' or 'no'
test('10. TEST when the input is true or no', () => {
  let testing1 = Lconst.from_pyrope('true');
  let testing2 = Lconst.from_pyrope('false');

  expect(testing1.explicit_str).not.toBeTruthy();
  expect(testing1.num).toBe(-1n);
  expect(testing1.bits).toBe(1n);

  expect(testing2.explicit_str).not.toBeTruthy();
  expect(testing2.num).toBe(0n);
  expect(testing2.bits).toBe(1n);
});

test('11. TEST when the encoding is not correct', () => {
  expect(() => {
    Lconst.from_pyrope('0g23');
  }).toThrow('ERROR: 0g23 unknown pyrope encoding (leading g)...');
});

// test, invalid characters
test('12. TEST invalid characters', () => {
  expect(() => {
    Lconst.from_pyrope('0b12345');
  }).toThrow('ERROR: 12345 binary encoding could not use 2');

  expect(() => {
    Lconst.from_pyrope('0xFFFG');
  }).toThrow('ERROR: 0xFFFG encoding could not use g');

  expect(() => {
    Lconst.from_pyrope('0d12344F');
  }).toThrow('ERROR: 0d12344F encoding could not use f');

  expect(() => {
    Lconst.from_pyrope('017778');
  }).toThrow('ERROR: 017778 encoding could not use 8');
});

test('13. TEST throw due to un-stringed input', () => {
  expect(() => {
    Lconst.from_pyrope(1);
  }).toThrow('the input must be a string');
});

// AND_OP
test('14. simple tests for AND operation', () => {
  const testing1 = Lconst.from_pyrope('0xFFFF');
  const testing2 = Lconst.from_pyrope('0xA1CD');
  const res = testing1.and_op(testing2);
  expect(res.num).toBe(41421n);
  expect(res.explicit_str).toBe(false);
  expect(res.bits).toBe(17n); // NOTICE: one extra bit for inexplicit sign bit "0"
});

test('15. TEST AND_OP compare with unknown', () => {
  // test 1: 0b010??1 and 0b000?01
  const a1 = Lconst.from_pyrope('0b010??1');
  const a2 = Lconst.from_pyrope('0b000?01');
  const resa = a1.and_op(a2);
  expect(resa.num).toBe(1n); // 0b000001
  expect(resa.unknown).toBe(4n); // 0b000100

  // test 2: 0b1?1001 and 0b111?01
  const b1 = Lconst.from_pyrope('0b1?1001');
  const b2 = Lconst.from_pyrope('0b111?01');
  const resb = b1.and_op(b2);
  expect(resb.num).toBe(41n); // 0b101001
  expect(resb.unknown).toBe(16n); //0?0000

  // test 3: 0b010??1 and 0b000?11
  const c1 = Lconst.from_pyrope('0b010??1');
  const c2 = Lconst.from_pyrope('0b000?11');
  const resc = c1.and_op(c2);
  expect(resc.num).toBe(1n); // 0b000001
  expect(resc.unknown).toBe(6n); //0000110
});

// OR_OP
test('16. simple tests for OR operation', () => {
  const testing1 = Lconst.from_pyrope('0xFFD_F');
  const testing2 = Lconst.from_pyrope('0x0_000');
  const res = testing1.or_op(testing2);
  expect(res.num).toBe(65503n);
  expect(res.explicit_str).toBe(false);
  expect(res.bits).toBe(17n);
});

test('17. TEST OR_OP compare with unknown', () => {
  const a1 = Lconst.from_pyrope('0b010??1');
  const a2 = Lconst.from_pyrope('0b000?11');
  const resa = a1.or_op(a2);
  expect(resa.num).toBe(19n); //0b010011
  expect(resa.unknown).toBe(4n); //0b100

  const b1 = Lconst.from_pyrope('0b010??1');
  const b2 = Lconst.from_pyrope('0b000?01');
  const resb = b1.or_op(b2);
  expect(resb.num).toBe(17n); //0b010001
  expect(resb.unknown).toBe(6n); // 0b000110
});

// test plus and minus

// test mul and div
test('18. complicated check, a divide by b', () => {
  const testing1a = Lconst.from_pyrope('50052096');
  const testing1b = Lconst.from_pyrope('0xBE_EF');
  expect(testing1a.div_op(testing1b).num).toBe(1024n);

  const testing2a = Lconst.from_pyrope('0');
  const testing2b = Lconst.from_pyrope('0xBEEF');
  const testing2c = Lconst.from_pyrope('-0b101');
  expect(testing2a.div_op(testing2b).num).toBe(0n);
  expect(testing2b.div_op(testing2a).num).toBe(16176n); // num is ?0, which is 16 bits
  expect(testing2c.div_op(testing2a).num).toBe(16177n); // num is ?1, which is 16 bits
});

test('19. complicated check, a multiply b', () => {
  const testing1a = Lconst.from_pyrope('1231532342345');
  const testing1b = Lconst.from_pyrope('0xBEEF');
  expect(testing1a.mult_op(testing1b).num).toBe(60196069361481255n);

  const testing2a = Lconst.from_pyrope('0b?');
  const testing2b = Lconst.from_pyrope('0b1');
  // Because we count one additional bit for unsigned value, so both  0b? and 0b1 has two bits.
  // thus, the answer should be '???0'
  //
  // Simple rule if only one side has unknowns:
  //
  // n:0b0 x 23 
  // U:0b1 x 23
  // 0b? x 2 = 0b?0
  // 0b? x 3 = 0b??
  // 0b? x 4 = 0b?00
  //
  // 0b?0? x 1 =   0b?0?
  // 0b?0? x 2 =  0b?0?0
  // 0b?0? x 3 =  0b????
  //
  // 0b?0? x 4 = 0b?0?00
  //  n:0   x 4 = 0b00000
  //  u:101 x 4 = 0b10100
  // 0b?0? x 5 = 0b??00?
  //  n:0
  //  u:101 x 5 =  11001
  //
  // NOT CLEAR if both sides have unknowns (just max bits all ??)
  // 0b?0 x 0b?1 (2x3 or 2x1 or 0x3 or 0x1)
  //             0b100
  //             0b010
  //             0b011
  //             0b001
  //             0b???
  // n:00, u:10
  // n:01, u:10
  //         10
  expect(testing2a.mult_op(testing2b).num).toBe(1061109552n);
});

// tests from string
test('20. complated check, from_string', () => {
  // I used the following online big number calculator to solve the answer before testing
  // https://www.calculator.net/big-number-calculator.html?cx=1%2C111%2C835%2C904&cy=70&cp=20&co=plus
  const test1 = Lconst.from_string('BEEF');
  const test2 = Lconst.from_string('?!');
  expect(test1.num).toBe(1111835974n);
  expect(test2.num).toBe(16161n);
  expect(test2.explicit_str && test1.explicit_str).toBe(true);
});

test('addition. complicated check. from_string', () => {
  const test1 = Lconst.from_string('BEEF');
  const test2 = Lconst.from_string('FFFF');
  expect(test1.and_op(test2).num).toBe(1111770182n);
});
// tests ADD_OP
test('21. complicated check, and_op', () => {
  const testing1a = Lconst.from_pyrope('0b1?10');
  const testing1b = Lconst.from_pyrope('0b10');
  // if a is 1?10, and b is 0b10, then a+b = ???00
  const res1 = testing1b.add_op(testing1a);
  expect(res1.num).toBe(0n);
  expect(res1.unknown).toBe(28n);

  const testing2a = Lconst.from_pyrope('0b1110');
  const testing2b = Lconst.from_pyrope('0b10');
  const res2 = testing2a.add_op(testing2b);
  expect(res2.num).toBe(16n);
  expect(res2.has_unknown).toBe(false); //Potential BUG, unknown should be undefined even though has_unknown == false
});
