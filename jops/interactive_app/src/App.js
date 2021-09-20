import React, { useState, useEffect } from 'react';
import Calculator from './calculator';
const Lconst = require('./Lconst/jops');

function getLconst(functionx, valuex) {
  if (functionx === 'from_pyrope') {
    return Lconst.from_pyrope(valuex);
  } else {
    return null;
  }
}

function calculator(function_A, value_A, function_B, value_B, operation) {
  console.log(function_A, value_A);
  const itemA = getLconst(function_A, value_A);
  const itemB = getLconst(function_B, value_B);
  if (itemA === null || itemB === null) {
    throw 'some error';
  } else if (operation === 'and') {
    return itemA.add_op(itemB);
  }
}

function App() {
  const testing = Lconst.from_pyrope('0xFFFFFFFFFFFFFFF');
  const b = testing.num;
  const build_functions = ['from_pyrope', 'from_binary'];
  const build_opeartion = ['and', 'or', 'add', 'div', 'mul'];

  const [selected_function_A, set_function_A] = useState('from_pyrope');
  const [value_A, set_value_A] = useState('0x0');
  const [selected_function_B, set_function_B] = useState('from_pyrope');
  const [value_B, set_value_B] = useState('0x0');
  const [selected_operation, set_operation] = useState('and');
  const [current_result, set_result] = useState();

  useEffect(() => {
    update_result();
  }, [selected_function_A, selected_function_B]);

  function update_valueA(new_value) {
    set_value_A(new_value);
  }

  function update_valueB(new_value) {
    set_value_B(new_value);
  }

  function update_result() {
    const ans = calculator('from_pyrope', '0x0', 'from_pyrope', '0x0', 'and');
    set_result(ans.num);
  }

  return (
    <div className="App">
      <div> Input A {`${selected_function_A}, value: ${value_A}`} </div>
      <div> Input B {`${selected_function_B}, value: ${value_B}`} </div>
      <div> Operation {`${selected_operation}`} </div>
      <div> Result {`${current_result}`}</div>
      <Calculator />
    </div>
  );
}

export default App;
