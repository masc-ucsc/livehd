""" C++ compile flags not needed globally are stored here
so that -Werror can be used without worrying about warnings in external packages
"""

COPTS = [
    "-std=c++23",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-error=deprecated-copy",  # abseil/abseil-cpp#948
    "-Wno-unknown-pragmas",
    "-Wno-error=deprecated",
    "-Wunused",
    "-Wshadow",
]
