#!/bin/bash

declare -a inputs=("fluid_example.v")

for input in ${inputs[@]}
do
  ./inou/yosys/lgyosys ./pass/lgopt_fluid/tests/${input}

  if [ $? -eq 0 ]; then
    echo "Successfully created graph from "${input}
  else
    echo "FAIL: lgyosys terminated with and error"
    exit 1
  fi

  ./inou/yaml/lgyaml --lgdb lgdb --graph_name fluid_example --yaml_output fluid_example.yaml

  if [ $? -eq 0 ]; then
    echo "Successfully lgyaml file "$a
  else
    echo "FAIL: lgyaml terminated with and error"
    exit 1
  fi

  ./pass/lgopt_fluid/lgopt_fluid --lgdb lgdb --graph_name fluid_example
  if [ $? -eq 0 ]; then
    echo "Successfully ran lgopt_fluid file "$a
  else
    echo "FAIL: lgopt_fluid terminated with and error"
    exit 1
  fi

done

echo "SUCCESS: all fluid test cases ended without errors"

