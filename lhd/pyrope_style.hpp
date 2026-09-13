// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace livehd::pyrope::style {

struct Options {
  size_t min_repeats          = 7;
  size_t max_block_statements = 128;
  size_t max_findings         = 20;
};

struct Range {
  uint32_t start_byte, end_byte;
  uint32_t start_line, start_column, end_line, end_column;
};

enum class Rule { RepeatedCode, LikelyUnrolledLoop, WholeTupleCopy, FlattenedBundleArguments, SingleDestinationConditional };

std::string_view rule_name(Rule rule);

struct RelatedLocation {
  Range       range;
  std::string message;
};

struct Finding {
  Range                                            range;
  Range                                            first_copy;
  size_t                                           statements, repetitions, score;
  bool                                             progressing;
  std::string                                      pattern;
  std::string                                      progression;
  Rule                                             rule = Rule::RepeatedCode;
  std::string                                      message;
  std::string                                      hint;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<RelatedLocation>                     related;
};

struct Report {
  bool                 partial        = false;
  size_t               total_findings = 0;
  std::vector<Finding> findings;
  std::vector<Range>   parse_errors;
};

// Source-only analysis: no imports, elaboration, or compiler state. The same
// report can be presented by the CLI or an editor. Throws on parser failure.
Report analyze(std::string_view source, const Options& options = {});

}  // namespace livehd::pyrope::style
