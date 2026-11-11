// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <string>
#include <string_view>

// A small Liberty library for the fast timing tests, in ps and fF, with linear tables so that every expected
// number can be computed by hand: v(slew, load) = a + 0.1 * slew + c * load, with `a` scaled by the corner.
//
//   INV   A -> Y negative unate     rise 10k + .1 s + 2 l, fall 15k + ...   slew rise 5k + .1 s + .2 l, fall 6k + ...
//   BUF   A -> Y positive unate     rise 20k, fall 22k
//   XOR2  A, B -> Y non unate       rise 25k, fall 27k
//   MUXC  A -> Y when "S"           rise 12k, fall 13k
//   FF    rising edge; CLK -> Q 30k / 40k (slew 50k / 60k); setup 10k / 12k; hold 4k / 6k; D max_transition 12
//   NEGFF falling-edge flip-flop (not timed)
namespace livehd::timing::fast::test {

inline std::string linear(const char* name, double a, double c, double k) {
  const double base = a * k, slope = 0.1;
  return std::format("{}(normal) {{ values(\"{:.9g}, {:.9g}\", \"{:.9g}, {:.9g}\"); }}",
                     name,
                     base,
                     base + 100 * c,
                     base + 100 * slope,
                     base + 100 * slope + 100 * c);
}
inline std::string comb(const char* related, const char* sense, double rise, double fall, double k, const char* when = "") {
  std::string text = std::format("timing() {{ related_pin : {}; timing_sense : {}; ", related, sense);
  if (*when != 0) {
    text += std::format("when : \"{}\"; ", when);
  }
  text += linear("cell_rise", rise, 2, k) + linear("cell_fall", fall, 2, k) + linear("rise_transition", 5, .2, k)
          + linear("fall_transition", 6, .2, k) + " }";
  return text;
}
inline std::string constant(const char* name, double value, double k) {
  return std::format("{}(setup) {{ values(\"{:.9g}, {:.9g}\", \"{:.9g}, {:.9g}\"); }}",
                     name,
                     value * k,
                     value * k,
                     value * k,
                     value * k);
}

inline std::string library_text(double k, std::string_view without = "") {
  std::string text = R"lib(
library(test) {
  time_unit : "1ps";
  capacitive_load_unit(1,ff);
  lu_table_template(normal) {
    variable_1 : input_net_transition;
    variable_2 : total_output_net_capacitance;
    index_1("0, 100"); index_2("0, 100");
  }
  lu_table_template(setup) {
    variable_1 : constrained_pin_transition;
    variable_2 : related_pin_transition;
    index_1("0, 100"); index_2("0, 100");
  }
)lib";
  const auto  add  = [&](std::string_view name, const std::string& body) {
    if (name != without) {
      text += body;
    }
  };
  add("INV",
      "cell(INV) { area : 3; pin(A) { direction : input; capacitance : 2; } pin(Y) { direction : output; function : \"!A\"; "
      "max_capacitance : 5; "
          + comb("A", "negative_unate", 10, 15, k) + " } }\n");
  add("BUF",
      "cell(BUF) { area : 4; pin(A) { direction : input; capacitance : 3; } pin(Y) { direction : output; function : \"A\"; "
          + comb("A", "positive_unate", 20, 22, k) + " } }\n");
  add("XOR2",
      "cell(XOR2) { area : 8; pin(A) { direction : input; capacitance : 2; } pin(B) { direction : input; capacitance : 2; } "
      "pin(Y) { direction : output; function : \"A^B\"; "
          + comb("A", "non_unate", 25, 27, k) + comb("B", "non_unate", 25, 27, k) + " } }\n");
  add("MUXC",
      "cell(MUXC) { area : 6; pin(A) { direction : input; capacitance : 2; } pin(S) { direction : input; capacitance : 2; } "
      "pin(Y) { direction : output; function : \"A\"; "
          + comb("A", "positive_unate", 12, 13, k, "S") + " } }\n");
  add("FF", "cell(FF) { area : 20; ff(IQ, IQN) { clocked_on : \"CLK\"; next_state : \"D\"; } "
            "pin(CLK) { direction : input; clock : true; capacitance : 1; } "
            "pin(D) { direction : input; capacitance : 1; max_transition : 12; "
            "timing() { related_pin : CLK; timing_type : setup_rising; " + constant("rise_constraint", 10, k) + constant("fall_constraint", 12, k)
                + " } timing() { related_pin : CLK; timing_type : hold_rising; " + constant("rise_constraint", 4, k)
                + constant("fall_constraint", 6, k) + " } } "
                  "pin(Q) { direction : output; function : \"IQ\"; timing() { related_pin : CLK; timing_type : rising_edge; "
                  "timing_sense : non_unate; "
                  + std::format("cell_rise(scalar) {{ values(\"{:.9g}\"); }} cell_fall(scalar) {{ values(\"{:.9g}\"); }} "
                                "rise_transition(scalar) {{ values(\"{:.9g}\"); }} fall_transition(scalar) {{ values(\"{:.9g}\"); }}",
                                30 * k, 40 * k, 50 * k, 60 * k)
                  + " } } }\n");
  add("NEGFF", "cell(NEGFF) { area : 22; ff(IQ, IQN) { clocked_on : \"!CLK\"; next_state : \"D\"; } "
               "pin(CLK) { direction : input; clock : true; capacitance : 1; } "
               "pin(D) { direction : input; capacitance : 1; timing() { related_pin : CLK; timing_type : setup_falling; "
               + constant("rise_constraint", 10, k) + " } } pin(Q) { direction : output; function : \"IQ\"; timing() { related_pin : CLK; "
                                                            "timing_type : falling_edge; timing_sense : non_unate; "
               + std::format("cell_rise(scalar) {{ values(\"{:.9g}\"); }}", 30 * k) + " } } }\n");
  text += "}\n";
  return text;
}

}  // namespace livehd::timing::fast::test
