const Lconst = require('./jops');

test('simple check, if it is a number', () => {
  const testing = new Lconst(3);
  expect(testing.num).toBe(3);
  expect(testing.bits).toBe(3);
  expect(testing.explicit_str).toBe(false);
})

test('simple check, if it is not a number', () => {
  const testing = new Lconst("3");
  expect(testing.num).toBe(0);
  expect(testing.bits).toBe(0);
  expect(testing.explicit_str).toBe(false);
})