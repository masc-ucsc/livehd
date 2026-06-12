""" C++ compile flags not needed globally are stored here
so that -Werror can be used without worrying about warnings in external packages
"""

COPTS = [
    "-Wall",
    "-Wextra",
    "-Werror",  # LiveHD C++ is warning-clean; external deps (MODULE.bazel) keep their own lenient flags
    "-Wno-error=deprecated-copy",  # abseil/abseil-cpp#948
    "-Wno-unknown-pragmas",
    "-Wno-error=deprecated",
    # GCC -O2 emits a false-positive -Warray-bounds on slang's SLANG_ENUM
    # toString() tables when inlined across the external-header boundary into
    # LiveHD code (e.g. inou/slang). -isystem can't suppress it because the
    # diagnostic is attributed to our call site; Clang (macOS) never emits it.
    # Demote to a warning so the opt build is consistent across platforms.
    "-Wno-error=array-bounds",
    "-Wunused",
    "-Wshadow",
]
