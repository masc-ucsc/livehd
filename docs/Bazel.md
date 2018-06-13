
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

## To create Python self contained par file

    bazel build //pyth:lgraph.par

Now, you can copy the bazel-bin/pyth/lgraph.par to any machine and it has all the python and libraries needed to run

## To create a fully static binary (for pip deployment?)

In the cc_binary, add linkopts = ['-static']

## To remove all the bazel (it should not be needed, but in case)

This command is useful for benchmarking build time, and in rare cases that
building in one machine breaks, and starts in another and it freezes.

    bazel clean --expunge

