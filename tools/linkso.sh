#!/bin/bash
#echo $@ >$1
ARGS=("$@")
SOLIB=$1
CC=$2
AR=$3

REST=""

for a in *.o
do
  if [ $a != '*.o' ]; then
    echo "ERROR: linkso.sh assumes no objects in sandbox before expansion"
    exit 3
  fi
done
STATIC_FOUND=0
## Start from 4 because ARGS array starts from 0 unlike $@
for ((i=2;i<$#;i++)); do
    #DEBUG echo $i" ${ARGS[$i]}" >>$1
    if [ "${ARGS[$i]}" = "-Wl,-Bstatic" ]; then
      STATIC_FOUND=1
    fi
    if [ $STATIC_FOUND -eq 1 ]; then
      REST+=" ${ARGS[$i]}"
    else
      $($AR -x ${ARGS[$i]})
    fi
done

#DEBUG echo "2---" >>$1
OBJS="*.o"

#DEBUG echo "4---" >>$1
#export PATH=$PATH:/usr/bin
CMD=$CC" -B/usr/bin -fuse-ld=gold -Wl,-no-as-needed -Wl,-z,relro,-z,now -fPIC -shared -o"$SOLIB" "$OBJS" "$REST

#echo $CMD
#echo $CMD >>$1
$($CMD)
exit $?
