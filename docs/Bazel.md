
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
    bazel build -c dbg //inou/json:lgjson

    # Release
    bazel build --compilation_mode=opt //inou/json:lgjson

    # Fast Build with assertions
    bazel build                        //inou/json:lgjson

## See the command line executed

    bazel build -s //core

## To run SHORT tests

The SHORT tests should have less than 15 minutes execution time for each
individual test. Those tests are used to gate commits to the main repo,
and to generate coverage reports.

    bazel test //...

To debug errors in the testing environment, you may want to keep the sandbox
files to check what may be going wrong. Use:

    bazel test //... --sandbox_debug --keep_state_after_build

## To run LONG tests

In addition to the short tests, there are sets of long tests that are run frequently
but not before every push to main line. The reason is that those are multi-hour
tests.

    bazel test --test_tag_filters "long1" //...

There are up to 8 long tests categories (long1, long2, long3...). Each of those
tests groups should last less than 4 hours when running in a dual core machine
(travis or azure).

## Debugging with bazel

First run the tests to see the failing one. Then run with debug options
the failing test. E.g:

    bazel run -c dbg //eprp:all

Increase logging level if wanted

    export LGRAPH_LOG=info
    bazel run -c dbg //eprp:all

To run with gdb

    bazel build -c dbg //eprp:eprp_test
    gdb bazel-bin/eprp/eprp_test
    >b Eprp::run
    >r

## Code coverage for all the tests used

    bazel build --collect_code_coverage ...
    # or better with runs
    bazel test --collect_code_coverage --test_output=all --nocache_test_results ...

## To download the dependent packages and apply patches (abc, bm,...)

No need to run this, as the bazel build will do it.

    bazel fetch ...

The downloaded code would be at bazel-lgraph/external/

## To create a fully static binary

In the cc binary, add linkopts = ['-static']

Notice that the lgshell still needs the directory inside
bazel-bin/main/lgshell.runfiles when using inou.yosys.\*

## To remove all the bazel (it should not be needed, but in case)

This command is useful for benchmarking build time, and in rare cases that
building in one machine breaks, and starts in another and it freezes.

    bazel clean --expunge

