const Lconst = require('./jops');

test('just a simple check...', () => {
  const testing = new Lconst(3);
  expect(testing.num).toBe(3);
  expect(testing.bits).toBe(3);
  expect(testing.explicit_str).toBe(false);
})