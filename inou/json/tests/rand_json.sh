#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

LGSHELL=./bazel-bin/main/lgshell

if [ ! -x $LGSHELL ]; then
  if [ -x ./main/lgshell ]; then
    LGSHELL=./main/lgshell
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi

for a in 1 4 20 100
do
	echo "inou.rand eratio:${a} seed:${a} name:rand_${a} |> inou.json.fromlg odir:tmp" | ${LGSHELL}
  if [ $? -ne 0 ]; then
    echo "FAIL: inou.json.fromlg rand_${a} terminated with and error"
    exit 1
  fi

	cp tmp/rand_${a}.json tmp/tmp_${a}.json
  echo "inou.json.tolg files:tmp/tmp_${a}.json " | ${LGSHELL}
  if [ $? -ne 0 ]; then
    echo "FAIL: inou.json.tolg terminated with and error"
    exit 1
  fi

  RES1=$(grep idx: tmp/tmp_${a}.json  | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum)
  RES2=$(grep idx: tmp/rand_${a}.json | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum)
	if [ "$RES1" != "$RES2" ]; then
		echo "mismatch in the comparison after lgjson for rand_${a}"
		exit 3
	fi
done

exit 0
