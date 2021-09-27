#!/bin/bash

SLANG=./bazel-bin/inou/slang/slang

if [ ! -f $SLANG ]; then
  if [ -f ./inou/slang/slang ]; then
    SLANG=./inou/slang/slang
    echo "slang is in $(pwd)"
  else
    echo "ERROR: could not find slang binary in $(pwd)";
    exit -2
  fi
fi

for file in inou/yosys/tests/trivial.v
do
  ${SLANG} --ast-json result.json ${file}
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "ERROR: could not direct execute slang with file:${file}!"
    exit $ret_val
  fi

  pts=$(basename $file)
  pts=${pts%.v}

	id_name=$(grep name result.json | grep ${pts})
  if [ "${id_name}" == "" ]; then
		echo "ERROR: could not find name:${pts} in json generate file for file:${file}"
    exit -3
  fi
done

exit 0
