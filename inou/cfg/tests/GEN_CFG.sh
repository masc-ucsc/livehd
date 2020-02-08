#!/bin/bash
rm -f *.cfg

function generate () {
  ./prp $1.prp | sort -n > y && column -t y > x && rm -f y && mv x $1.cfg
}

generate function_call
generate nested_if
generate tuple
generate ssa_if
generate ssa_nested_if

# ./prp function_call.prp | sort -n  > y && column -t y > x && rm -f y && mv x function_call.cfg
# ./prp nested_if.prp     | sort -n  > y && column -t y > x && rm -f y && mv x nested_if.cfg
# ./prp tuple.prp         | sort -n  > y && column -t y > x && rm -f y && mv x tuple.cfg
# ./prp ssa_if.prp        | sort -n  > y && column -t y > x && rm -f y && mv x ssa_if.cfg
# ./prp ssa_nested_if.prp | sort -n  > y && column -t y > x && rm -f y && mv x ssa_nested_if.cfg


