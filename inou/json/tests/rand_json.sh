#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.


LGSHELL=./bazel-bin/main/lgshell

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi

for a in 1 4 20 100
do
	echo "inou.rand eratio:$a seed:$a name:rand_$a |> inou.json.fromlg output:output.json" | ${LGSHELL}

  if [ $? -eq 0 ]; then
    echo "Successfully lgrand file "$a
  else
    echo "FAIL: lgrand terminated with and error"
    exit 1
  fi

	cp output.json input.json
  echo "inou.json.tolg file:input.json " | ${LGSHELL}
  if [ $? -eq 0 ]; then
    echo "Successfully lgjson file "$a
  else
    echo "FAIL: lgjson terminated with and error"
    exit 1
  fi

  RES1=$(grep idx: input.json  | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum)
  RES2=$(grep idx: output.json | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum)
	if [ "$RES1" != "$RES2" ]; then
		echo "mismatch in the comparison after lgjson"
		exit 3
	fi
done

exit 0
