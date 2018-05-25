#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "usage func_liberty_json.sh <library.lib> #exactly one argument required"
  exit 1
fi

awk 'BEGIN{print "{\n\"cells\": ["}{if($0 ~ /cell /) { if(cell==1) { if(inps!="") { print inps"],"; inps = "" }; if(out !="") { print out"]"; out ="" }; print "},";}; print "{\n\"cell\": "$2","; cell=1 }; if($0 ~ / pin /) { name=$2 ;}; if($0 ~ /direction/) { if($0 ~ /input/) { if(inps=="") { inps = "\"inps\": ["name"" } else { inps = inps", "name } } if($0 ~ /output/ ) { if(out=="") { out = "\"outs\": ["name; } else { out = out", "name } } } }END{if(cell==1){if(inps!="") { print inps"],"; inps = "" }; if(out !="") { print out"]"; out ="" }; print "}";}; print "]\n}"}' <$1 | tr '(' '"' | tr ')' '"' | sed '/,/{ N; s/,\n}/\n}/ }' | sed s/\"\"/\"/g

