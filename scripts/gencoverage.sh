#!/bin/sh

#bazel coverage -k //...

# -k keep going, --experimental_cc_coverage for gcc newer patches
bazel coverage -k --experimental_cc_coverage  //...

for a in `bazel query "tests(//...)" 2>/dev/null | grep ^\/ | sed -e 's/^\//.\/bazel-bin/g' | sed -e 's/:/\//g'`
do
  if [[ $a =~ "long" ]]; then
    echo "Not using ${a} for coverage"
  else
    echo "coverage for ${a}"
    ${a}
  fi
done

