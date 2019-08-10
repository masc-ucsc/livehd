#!/bin/bash


for a in cops core eprp inou live main pass
do
  lcov --ignore-errors source --capture --rc geninfo_auto_base=1 --rc lcov_branch_coverage=1 --compat-libtool --base-directory . --directory bazel-out/k8-fastbuild/bin/${a} --exclude '/usr/*' --exclude 'external/*' --output-file cov/coverage_${a}.info
echo $a
done

if [ -s cov/coverage.info ]; then
  cp -a cov/coverage.info cov/coverage_prev_run.info
fi
LCOV_ADD=""
for a in cops core eprp inou live main pass
do
  if [ -s cov/coverage_${a}.info ]; then
    LCOV_ADD="${LCOV_ADD} --add-tracefile cov/coverage_${a}.info"
  else
    echo "Empty coverage file cov/coverage_${a}.info"
  fi
done

echo $LCOV_ADD
lcov $LCOV_ADD --output-file cov/coverage.info

