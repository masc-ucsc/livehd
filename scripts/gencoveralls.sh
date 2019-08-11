#!/bin/bash

lcov --ignore-errors source --capture --rc geninfo_auto_base=1 --rc lcov_branch_coverage=1 --compat-libtool --base-directory . --directory bazel-out/k8-fastbuild/bin/ --exclude '/usr/*' --exclude 'external/*' --output-file cov/coverage_all.info 2>cov_warnings.log

if [ -s cov/coverage.info ]; then
  cp -a cov/coverage.info cov/coverage_prev_run.info
  lcov cov/coverage_prev_run.info cov/coverage_all.info --output-file cov/coverage.info
else
  cp -a cov/coverage_all.info cov/coverage.info
fi

