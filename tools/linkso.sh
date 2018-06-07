#!/bin/bash
#echo $@ >$1
ARGS=("$@")
SOLIB=$1
CC=$2
AR=$3
OBJ_DIR=$4
OBJ_LIB=$5

REST=""
## Start from 4 because ARGS array starts from 0 unlike $@
for ((i=4;i<$#;i++)); do
    #DEBUG echo $i" ${ARGS[$i]}" >>$1
    REST+=" ${ARGS[$i]}"
done

#DEBUG echo "2---" >>$1
for a in *.o
do
  if [ $a != '*.o' ]; then
    echo "ERROR: linkso.sh assumes no objects in sandbox before expansion"
    exit 3
  fi
done
$($AR -x $OBJ_LIB)
OBJS="*.o"

#DEBUG echo "4---" >>$1
#export PATH=$PATH:/usr/bin
CMD=$CC" -B/usr/bin -fuse-ld=gold -Wl,-no-as-needed -Wl,-z,relro,-z,now -fPIC -shared -o"$SOLIB" "$OBJS" "$REST

#echo $CMD >>$1
$($CMD)
exit $?
