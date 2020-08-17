

# git clean -fdx 
bazel build -c opt //...

for a in `bazel query "tests(//...)" 2>/dev/null | grep ^\/ | sed -e 's/^\//.\/bazel-bin/g' | sed -e 's/:/\//g'`
do
  perf stat -i ${a}.perf_stat ${a}
done

