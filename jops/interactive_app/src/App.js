import React, { useState, useEffect } from 'react';
const Lconst = require('./Lconst/jops');

function getLconst(functionx, valuex) {
  if (functionx === 'from_pyrope') {
    return Lconst.from_pyrope(valuex);
  } else if (functionx === 'from_binary') {
    return Lconst.from_binary(valuex);
  } else if (functionx === 'from_string') {
    return Lconst.from_string(valuex);
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
  } else if (operation === 'div_op') {
    return itemA.div_op(itemB);
  } else if (operation === 'mul_op') {
    return itemA.mult_op(itemB);
  } else if (operation === 'xor_op') {
    return itemA.div_op(itemB);
  }
}

function App() {
  const default_Lconst = new Lconst();
  const build_functions = [
    'from_pyrope',
    'from_binary',
    'from_string',
    'to_pyrope',
  ];
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
  const [current_result, set_result] = useState(default_Lconst);
  const [cal_engine, set_engine] = useState(true);

  useEffect(() => {
    const update_result = () => {
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
        /* some code dealing with the errorc */
      }
      set_result(ans);
    };
    update_result();
    // next line may disable the warning
    // reason: calculate the ans.num only after the user click a button which would trigger 'cal_engine'
    // eslint-disable-next-line react-hooks/exhaustive-deps
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
        <div> engine {`${cal_engine}`}</div>
        <div> ---data of result--- </div>
        <div> num: {`${current_result.num}`}</div>
        <div> unknown: {`${current_result.unknown}`}</div>
        <div> bits: {`${current_result.bits}`} </div>
        <div> explicit_str: {`${current_result.explicit_str}`}</div>
        <div> has_unknown: {`${current_result.has_unknown}`}</div>
      </div>
    </div>
  );
}

export default App;
