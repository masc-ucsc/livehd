import React, { useState, useEffect } from 'react';
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
    throw new Error('some error');
  } else if (operation === 'and_op') {
    return itemA.and_op(itemB);
  } else if (operation === 'or_op') {
    return itemA.or_op(itemB);
  } else if (operation === 'add_op') {
    return itemA.add_op(itemB);
  }
}

function App() {
  const build_functions = ['from_pyrope', 'from_binary', 'to_pyrope'];
  const build_opeartions = [
    'and_op',
    'or_op',
    'add_op',
    'div_op',
    'mul_op',
    'xor_op',
  ];

  const [selected_function_A, set_function_A] = useState('from_pyrope');
  const [value_A, set_value_A] = useState('0x0');
  const [selected_function_B, set_function_B] = useState('from_pyrope');
  const [value_B, set_value_B] = useState('0x0');
  const [selected_operation, set_operation] = useState('and_op');
  const [current_result, set_result] = useState();
  const [cal_engine, set_engine] = useState(true);

  useEffect(() => {
    function update_result() {
      let ans;
      try {
        ans = calculator(
          selected_function_A,
          value_A,
          selected_function_B,
          value_B,
          selected_operation
        );
      } catch (err) {
        /* some code dealing with the error */
      }
      set_result(ans.num);
    }
    update_result();
  }, [cal_engine]);

  function run_engine() {
    const new_value = cal_engine ? false : true;
    set_engine(new_value);
  }

  function update_valueA(new_value) {
    set_value_A(new_value);
  }

  function update_function_A(new_value) {
    set_function_A(new_value);
  }

  function update_valueB(new_value) {
    set_value_B(new_value);
  }

  function update_function_B(new_value) {
    set_function_B(new_value);
  }

  // Note!!! It seems like we cannot use map function directly in render (in the case of react function)
  const listFunctions = build_functions.map((each) => {
    return <option value={each}> {each} </option>;
  });

  const listOperations = build_opeartions.map((each) => {
    return <option value={each}> {each} </option>;
  });

  return (
    <div className="App">
      <div id="handle_A">
        Input A{'  '}
        <input
          type="text"
          placeholder="0x0"
          onChange={(e) => update_valueA(e.target.value)}
        />
        <select onChange={(e) => update_function_A(e.target.value)}>
          {listFunctions}
        </select>
      </div>

      <div id="handle_B">
        Input B{' '}
        <input
          type="text"
          placeholder="0x0"
          onChange={(e) => update_valueB(e.target.value)}
        />
        <select onChange={(e) => update_function_B(e.target.value)}>
          {listFunctions}
        </select>
      </div>

      <div id="handle_op">
        Operation{' '}
        <select onChange={(e) => set_operation(e.target.value)}>
          {listOperations}
        </select>
      </div>

      <div id="calculate">
        {' '}
        <button onClick={(e) => run_engine()}> calculate! </button>{' '}
      </div>
      <div id="testing area">
        <div> --------testing area------- </div>
        <div> Input A {`${selected_function_A}, value: ${value_A}`} </div>
        <div> Input B {`${selected_function_B}, value: ${value_B}`} </div>
        <div> Operation {`${selected_operation}`} </div>
        <div> Result {`${current_result}`}</div>
        <div> engine {`${cal_engine}`}</div>
      </div>
    </div>
  );
}

export default App;
