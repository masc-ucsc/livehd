
Test that are contract_* are consider contracts and LLMs can not modify them.

Similar with benchmarks
```
bazel test -c opt //benchmark:constprop_contract --test_output=streamed --cache_test_results=no
```
