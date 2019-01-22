#!/bin/sh

mkdir -p cov
rm -f cov/coverage.*
#bazel coverage -k //...

# -k keep going, --experimental_cc_coverage for gcc newer patches
bazel coverage -k --experimental_cc_coverage  //...

for a in `bazel query "tests(//...)" 2>/dev/null | grep ^\/ | sed -e 's/^\//.\/bazel-bin/g' | sed -e 's/:/\//g'`
do
  echo $a
  echo `${a}`
done

