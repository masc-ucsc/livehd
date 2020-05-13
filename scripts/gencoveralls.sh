#!/bin/bash

#lcov --ignore-errors source --capture --rc geninfo_auto_base=1 --rc lcov_branch_coverage=1 --compat-libtool --base-directory . --directory bazel-out/k8-fastbuild/bin/ --exclude '/usr/*' --exclude '*external*' --output-file cov/coverage.info 2>cov_warnings.log
lcov --ignore-errors source --capture --rc geninfo_auto_base=1 --rc lcov_branch_coverage=0 --no-branch-coverage --compat-libtool --base-directory . --directory bazel-out/k8-fastbuild/bin/ --exclude '/usr/*' --exclude '*external*' --output-file cov/coverage.info 2>cov_warnings.log

#  lcov cov/coverage_prev_run.info cov/coverage_all.info --output-file cov/coverage.info
