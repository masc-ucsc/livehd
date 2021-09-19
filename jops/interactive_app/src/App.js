import React from 'react';
import Calculator from './calculator';
const Lconst = require('./Lconst/jops');

function App() {
  const testing = Lconst.from_pyrope('0xF');

  return (
    <>
      {testing.num}
      <Calculator />
    </>
  );
}

export default App;
