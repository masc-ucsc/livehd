#!/bin/bash

for a in 0 4 100
do
	./inou/rand/lgrand --rand_eratio $a --rand_seed $a
  if [ $? -eq 0 ]; then
    echo "Successfully lgrand file "$a
  else
    echo "FAIL: lgrand terminated with and error"
    exit 1
  fi

	cp output.json input.json
	./inou/json/lgjson -i input.json
  if [ $? -eq 0 ]; then
    echo "Successfully lgjson file "$a
  else
    echo "FAIL: lgjson terminated with and error"
    exit 1
  fi

	RES1=`grep idx: input.json  | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum`
	RES2=`grep idx: output.json | sort | uniq -c | sort -t: -k1 -n | cut -d: -f1 | cksum`
	if [ "$RES1" != "$RES2" ]; then
		echo "missmatch in the comparision after lgjson"
		exit 3
	fi
done

exit 0
