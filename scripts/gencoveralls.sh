#!/bin/bash

for a in cloud cops core eprp inou live main meta pass
do
  lcov --ignore-errors source --capture --rc geninfo_auto_base=1 --rc lcov_branch_coverage=1 --compat-libtool --base-directory . --directory bazel-out/k8-fastbuild/bin/${a} --exclude '/usr/*' --exclude 'external/*' --output-file cov/coverage_${a}.info
echo $a
done

LCOV_ADD=""
for a in cloud cops core eprp inou live main meta pass
do
  if [ -s cov/coverage_${a}.info ]; then
    LCOV_ADD="${LCOV_ADD} --add-tracefile cov/coverage_${a}.info"
  else
    echo "Empty coverage file cov/coverage_${a}.info"
  fi
done

echo $LCOV_ADD
lcov $LCOV_ADD --output-file cov/coverage.info

if [ -s cov/coverage.info ]; then
  coveralls-lcov --repo-token Z2cNEUdoWLokSj16laePFXdCWIwckDRHK cov/coverage.info >/dev/null
  curl -s https://codecov.io/bash >cov/codecov
  chmod 755 cov/codecov
  ./cov/codecov -f 'cov/coverage.info' -t becc0c47-6817-4ba5-966c-3fc4dbb376ff >/dev/null
fi

