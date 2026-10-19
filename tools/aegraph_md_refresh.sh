#!/bin/sh
# Refresh the checked-in pass/aegraph/RULES.md from the rulegen output (tools/aegraph.bzl):
#   bazel run //pass/aegraph:aeg_rules_refresh
# usage (set by the macro): aegraph_md_refresh.sh GENERATED WORKSPACE_RELATIVE_TARGET
set -e
cp -f "$1" "$BUILD_WORKSPACE_DIRECTORY/$2"
chmod 644 "$BUILD_WORKSPACE_DIRECTORY/$2"
echo "refreshed $2"
