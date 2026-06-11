#!/usr/bin/env bash

set -euo pipefail

missing_tools=()
for tool in bazel git awk lcov genhtml; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing_tools+=("$tool")
  fi
done

if [ "${#missing_tools[@]}" -ne 0 ]; then
  echo "getcov: missing required tool(s): ${missing_tools[*]}" >&2
  echo "getcov: install them first. On macOS, lcov and genhtml come from: brew install lcov" >&2
  exit 1
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

cov_dir="$repo_root/cov"
raw_lcov="$cov_dir/livehd.raw.lcov"
filtered_lcov="$cov_dir/livehd.main-cpp.lcov"
tracked_files="$cov_dir/tracked_cpp_main_files.txt"
html_dir="$cov_dir/html"

mkdir -p "$cov_dir"

# On macOS the autoconfigured clang toolchain emits LLVM-native coverage
# (.profraw), but llvm-profdata/llvm-cov live inside the Xcode toolchain and
# are not on PATH, so rules_cc's autoconfig silently drops them and every test
# fails in collect_cc_coverage.sh with "line 99: : command not found".
# Point the toolchain at them explicitly and ask for lcov conversion per test.
# NOTE: the --repo_env flags re-configure local_config_cc, so the first plain
# `bazel build/test` after a coverage run recompiles once (and vice versa).
extra_flags=()
if command -v xcrun >/dev/null 2>&1; then
  extra_flags+=(
    "--repo_env=BAZEL_LLVM_COV=$(xcrun --find llvm-cov)"
    "--repo_env=BAZEL_LLVM_PROFDATA=$(xcrun --find llvm-profdata)"
    "--experimental_generate_llvm_lcov"
  )
fi

bazel_status=0
bazel coverage \
  -c opt \
  --combined_report=lcov \
  ${extra_flags[@]+"${extra_flags[@]}"} \
  --instrumentation_filter='^//(core|graph|lnast|parser|inou|lhd|lsp|pass|upass|ware|simlib|packages)(/|:)' \
  //... || bazel_status=$?

# Exit code 3 = some tests failed but the build succeeded; coverage was still
# collected for the passing tests, so keep going (flaky tests would otherwise
# kill the whole report). Anything else is a real build/infra failure.
if [ "$bazel_status" -ne 0 ] && [ "$bazel_status" -ne 3 ]; then
  exit "$bazel_status"
fi
if [ "$bazel_status" -eq 3 ]; then
  echo "getcov: WARNING: some tests failed (see bazel output above); report covers passing tests only" >&2
fi

coverage_report="$(bazel info output_path)/_coverage/_coverage_report.dat"
if [ ! -f "$coverage_report" ]; then
  echo "getcov: Bazel coverage report not found: $coverage_report" >&2
  exit 1
fi

cp "$coverage_report" "$raw_lcov"

git ls-files -- \
  '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' \
  ':(exclude)samples/**' \
  ':(exclude)**/test/**' \
  ':(exclude)**/tests/**' \
  ':(exclude)*_test.*' \
  >"$tracked_files"

awk -v root="$repo_root/" '
  function canon(path) {
    sub("^" root, "", path)
    sub("^\\./", "", path)
    sub("^.*/execroot/[^/]+/", "", path)
    return path
  }

  FNR == NR {
    keep[$0] = 1
    next
  }

  /^SF:/ {
    path = canon(substr($0, 4))
    emit = (path in keep)
    record = $0 ORS
    next
  }

  {
    record = record $0 ORS
    if ($0 == "end_of_record") {
      if (emit) {
        printf "%s", record
      }
      record = ""
      emit = 0
    }
  }
' "$tracked_files" "$raw_lcov" >"$filtered_lcov"

# llvm-cov-exported lcov data trips lcov 2.x consistency checks (lambda
# function records marked not-hit on lines that are hit); downgrade to warnings.
# Note: lcov 2.x requires --ignore-errors BEFORE --summary.
lcov --ignore-errors inconsistent,unsupported --summary "$filtered_lcov"

rm -rf "$html_dir"
genhtml --ignore-errors inconsistent,unsupported "$filtered_lcov" --output-directory "$html_dir"

echo
echo "Raw LCOV:      $raw_lcov"
echo "Filtered LCOV: $filtered_lcov"
echo "HTML report:   $html_dir/index.html"
if [ "$bazel_status" -eq 3 ]; then
  echo "getcov: WARNING: some tests failed; their coverage is not included" >&2
fi
