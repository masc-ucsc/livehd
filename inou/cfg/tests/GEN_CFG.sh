#!/bin/bash
rm -f *.cfg

function generate () {
  ./prp $1.prp | sort -n > y && column -t y > x && rm -f y && mv x $1.cfg
}

generate simple_tuple
generate function_call
generate tuple
generate tuple_if
generate tuple_if2
generate nested_if
generate if
generate if2
generate if3_err
generate nested_if
generate nested_if_err
generate ssa_rhs
generate logic

# ./prp simple_tuple.prp  | sort -n  > y && column -t y > x && rm -f y && mv x simple_tuple.cfg
# ./prp function_call.prp | sort -n  > y && column -t y > x && rm -f y && mv x function_call.cfg
# ./prp tuple.prp         | sort -n  > y && column -t y > x && rm -f y && mv x tuple.cfg
# ./prp tuple_if.prp      | sort -n  > y && column -t y > x && rm -f y && mv x tuple_if.cfg
# ./prp tuple_if2.prp     | sort -n  > y && column -t y > x && rm -f y && mv x tuple_if2.cfg
# ./prp if.prp            | sort -n  > y && column -t y > x && rm -f y && mv x if.cfg
# ./prp if2.prp           | sort -n  > y && column -t y > x && rm -f y && mv x if2.cfg
# ./prp if3_err.prp       | sort -n  > y && column -t y > x && rm -f y && mv x if3_err.cfg
# ./prp nested_if.prp     | sort -n  > y && column -t y > x && rm -f y && mv x nested_if.cfg
# ./prp nested_if_err.prp | sort -n  > y && column -t y > x && rm -f y && mv x nested_if_err.cfg


