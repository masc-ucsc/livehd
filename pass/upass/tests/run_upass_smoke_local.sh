#!/bin/sh

set -eu

cd "$(dirname "$0")/../../.."
PATH=/opt/homebrew/opt/bison/bin:$PATH bazel test //pass/upass:upass_smoke_suite --test_output=errors
