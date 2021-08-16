const Lconst = require('./jops');

test('simple check, if it is a number', () => {
  let testing = Lconst.from_pyrope('0x1');
  let testing2 = Lconst.from_pyrope('0x2');
  let testing3 = testing.xor_op(testing2);
  expect(testing3.to_pyrope()).toBe('0x3'); 
});