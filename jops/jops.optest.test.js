const Lconst = require('./jops');



test('TEST from_pyrope, underscore', () => {
  let testing = Lconst.from_pyrope('0x__ab_c_');
  expect(String(testing.num)).toBe('2748');  
});

test('TEST from_pyrope & to_pyrope, a trival test', () => {
  let testing = Lconst.from_pyrope('0x1');
  let testing2 = Lconst.from_pyrope('0x2');
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe('0x3'); 
});

test('TEST from_pyrope & to_pyrope, a average test', () => {
  let testing = Lconst.from_pyrope('0x12352353464564234526246__345723564756345');
  let testing2 = Lconst.from_pyrope('0x2435234655463_457_6543__2545324564136161346');
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe('0x251671736122621551114703061247452637003');
})