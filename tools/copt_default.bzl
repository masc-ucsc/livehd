""" C++ compile flags not needed globally are stored here
so that -Werror can be used without worrying about warnings in external packages
"""

COPTS = [
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-unknown-pragmas",
    "-Wunused",
    "-Wshadow",
]
