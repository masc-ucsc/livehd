
Useful commands:

## List bazel targets starting from top directory

    bazel query '...'

## List bazel targets starting from any directory

    bazel query 'inou/...'

## List files needed for a given target

   bazel query "deps(//inou/lefdef:lglefdef)" 

## Release vs fastbuild (default) vs debug

    # Debug
    bazel build --compilation_mode=dbg //inou/json:lgjson
    # Release
    bazel build --compilation_mode=opt //inou/json:lgjson
    # Fast Build with assertions
    bazel build                        //inou/json:lgjson

## See the command line executed

    bazel build -s //core

## To run all the tests in the system

    bazel test //...

## Debugging with bazel

First run the tests to see the failing one. Then run with debug options
the failing test. E.g:

    bazel run -c dbg //pyth:test_core1

If the test has python, you must call the gdb with python and then pass as
argument the test. Sample session:

    gdb `which python3`  # python3
    >r bazel-bin/pyth/test_core1
    >b LGraph_Base::LGraph_Base
    >r

## Code coverage for all the tests used

    bazel build --collect_code_coverage ...
    # or better with runs
    bazel test --collect_code_coverage --test_output=all --nocache_test_results ...

## To download the dependent packages and apply patches (abc)

No need to run this, as the bazel build will do it.

    bazel fetch ...

The downloaded code would be at bazel-lgraph/external/abc/

## To create Python self contained par file

    bazel build //pyth:ptlgraph.par

Now, you can copy the bazel-bin/pyth/ptlgraph.par to any machine and it has all the python and libraries needed to run

## To create a fully static binary (for pip deployment?)

In the cc binary, add linkopts = ['-static']

## To remove all the bazel (it should not be needed, but in case)

This command is useful for benchmarking build time, and in rare cases that
building in one machine breaks, and starts in another and it freezes.

    bazel clean --expunge

