const Lconst = require('./jops');

// test, when the input is in hex
test('TEST from_pyrope, underscore', () => {
  let testing = Lconst.from_pyrope('0x__ab_c_');
  expect(String(testing.num)).toBe('2748');
});

test('TEST from_pyrope, + and - sign', () => {
  let hex_postiveWithOutSign = Lconst.from_pyrope('0xFE_E_B');
  let hex_postiveWithSign = Lconst.from_pyrope('+0x_FEEB_');

  expect(hex_postiveWithOutSign.num).toBe(65259n);
  expect(hex_postiveWithSign.num).toBe(65259n);

  // !!!!! here I need to confirm on thing: as we know, in pyrope, hex value is always unsigned, so it is impossible to have -0x_FEEB as take in value
  expect(() => {
    Lconst.from_pyrope('-0x_FEEB');
  }).toThrow();
});

test('TEST from_pyrope & to_pyrope, a trival test', () => {
  let testing = Lconst.from_pyrope('0x1');
  let testing2 = Lconst.from_pyrope('0x2');
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe('0x3');
});

test('TEST from_pyrope & to_pyrope, a average test', () => {
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
test('TEST when the input is a signed binary', () => {
  const testing1 = Lconst.from_pyrope('0sb1110??1');
  const testing2 = Lconst.from_pyrope('0sb0110??1');
  const testing3 = Lconst.from_pyrope('-0b1110??1');

  expect(testing1.num).toBe(-15n);
  expect(testing1.unknown).toBe(6n);
  expect(testing1.bits).toBe(5); // notice that the extra '1' is not needed

  expect(testing2.num).toBe(49n);
  expect(testing2.unknown).toBe(6n);
  expect(testing2.bits).toBe(7);

  // !!!! not sure; wheter this result is correct or not
  expect(testing3.num).toBe(-113n);
  expect(testing3.unknown).toBe(6n);
  expect(testing3.bits).toBe(8);
});

test('TEST when the input is an unsigned binary', () => {
  const testing1 = Lconst.from_pyrope('0b110001');
  expect(testing1.num).toBe(49n);
  expect(testing1.bits).toBe(7); // extra non-explict bit '0' is required for unsigned string
});

test('TESt only binary number could be signed', () => {
  expect(() => {
    Lconst.from_pyrope('0sd12345');
  }).toThrow();

  expect(() => {
    Lconst.from_pyrope('0sxFEEB');
  }).toThrow();
});

// test, when the input is in decimal
test('TEST when the input is decimal', () => {
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
test('TEST when the input is signed oct', () => {
  const testing1 = Lconst.from_pyrope('0777_777_777777777');
  const testing2 = Lconst.from_pyrope('0o777_777_777777777');
  expect(testing1.num).toBe(35184372088831n);
  expect(testing1.num).toBe(testing2.num);
});

// test, when the input is 'true' or 'no'
test('TEST when the input is true or no', () => {
  let testing1 = Lconst.from_pyrope('true');
  let testing2 = Lconst.from_pyrope('false');

  expect(testing1.explicit_str).not.toBeTruthy();
  expect(testing1.num).toBe(-1);
  expect(testing1.bits).toBe(1);

  expect(testing2.explicit_str).not.toBeTruthy();
  expect(testing2.num).toBe(0);
  expect(testing2.bits).toBe(1);
});

test('TEST when the encoding is not correct', () => {
  expect(() => {
    Lconst.from_pyrope('0g23');
  }).toThrow('ERROR: 0g23 unknown pyrope encoding (leading g)...');
});

// test, invalid characters
test('TEST invalid characters', () => {
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

test('TEST throw due to un-stringed input', () => {
  expect(() => {
    Lconst.from_pyrope(1);
  }).toThrow('the input must be a string');
});
