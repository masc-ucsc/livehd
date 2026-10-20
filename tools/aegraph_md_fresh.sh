#!/bin/sh
# Staleness test for the generated pass/aegraph/RULES.md (tools/aegraph.bzl).
# usage: aegraph_md_fresh.sh CHECKED_IN GENERATED
if diff -u "$1" "$2"; then
  exit 0
fi
echo
echo "pass/aegraph/RULES.md is stale: it is generated from rules/*.rules (never edit it by hand)."
echo "Refresh it with:"
echo "  bazel run //pass/aegraph:aeg_rules_refresh"
exit 1
