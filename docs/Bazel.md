
Useful commands:

## List bazel targets starting from top directory

    bazel query '...'

## List bazel targets starting from any directory

    bazel query 'inou/...'

## Release vs fastbuild (default) vs debug

    # Debug
    bazel build --compilation_mode=dbg //inou/json:lgjson
    # Release
    bazel build --compilation_mode=opt //inou/json:lgjson
    # Fast Build with assertions
    bazel build                        //inou/json:lgjson

## See the command line executed

    bazel build -s //core

## To create a fully static binary (for pip deployment?)

 In the cc_binary, add linkopts = ['-static']

## To remove all the bazel (it should not be needed, but in case)

    bazel clean --expunge

