#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

declare -a inputs=("ispd18_sample" "ispd18_sample2" "ispd18_sample3")

TEMP=$(getopt -o s:: --long source:: -n 'lefdef.sh' -- "$@")
eval set -- "$TEMP"

OPT_LGRAPHSRC=""
while true ; do
    case "$1" in
        -s|--source)
          case "$2" in
              "") shift 2 ;;
              *) OPT_LGRAPHSRC=$2 ; shift 2 ;;
          esac ;;
        --)
          shift
          break
          ;;
        *)
          echo "Option $1 not recognized!"
          exit 1
          ;;
    esac
done

if [ "$OPT_LGRAPHSRC" == "" ]; then
  echo "--source required"
  exit 1
fi

if [ ! -d $OPT_LGRAPHSRC ]; then
  echo "could find lgraph src in $OPT_LGRAPHSRC"
  exit 1
fi

for input in ${inputs[@]}
do

  dir=$OPT_LGRAPHSRC/test/benchmarks/$input/
  name=$input

  echo "./inou/lefdef/lglefdef --lef_file $dir/$name.lef --def_file $dir/$name.def --lgdb lgdb_${name} --log log_${name}"
  ./inou/lefdef/lglefdef --lef_file $dir/$name.lef --def_file $dir/$name.def --lgdb lgdb_${name} --log log_${name}

  if [ $? -ne 0 ]; then
    echo "lglefdef failed for input $name"
    exit 1
  fi

done

exit 0
