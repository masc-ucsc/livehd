#!/bin/sh

mkdir -p cov
rm -f ./cov/coverage.*

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

./scripts/gencoveralls.sh

echo "rtp"
if [ -s cov/coverage.info ]; then
  gem install coveralls-lcov
  coveralls-lcov --repo-token Z2cNEUdoWLokSj16laePFXdCWIwckDRHK cov/coverage.info >/dev/null

  echo "coverall"
  curl -s https://codecov.io/bash >cov/codecov
  chmod 755 cov/codecov
  ./cov/codecov -f 'cov/coverage.info' -t becc0c47-6817-4ba5-966c-3fc4dbb376ff >/dev/null
fi

