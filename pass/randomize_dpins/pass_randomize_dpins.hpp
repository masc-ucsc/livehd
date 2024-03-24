//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include "lgraph.hpp"
#include "cell.hpp"
#include "node.hpp"
#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "perf_tracing.hpp"
//#define FOR_DBG

class Pass_randomize_dpins : public Pass {
private:
  std::vector<Node_pin::Compact_flat> io_pins_vec;
  std::vector<Node_pin::Compact_flat> comb_pins_vec;
  std::vector<Node_pin::Compact_flat> flop_pins_vec;
  std::vector<Node_pin::Compact_flat> combNflop_pins_vec;
  std::vector<Node_pin::Compact_flat> random_selected_pins_vec;

protected:
  static void randomize(Eprp_var &var);
  void collect_vectors_from_lg(Lgraph *lg, float noise_perc, bool comb_only);
  void print_vec(std::vector<Node_pin::Compact_flat>& vec_to_print) const;
  void print_nodePinCF_details(const Node_pin::Compact_flat& np_cf  ) const ;
  void annotate_lg(Lgraph *lg);

  template<typename T>
  std::vector<T> chooseMRandomElements(std::vector<T>& inputVector, int m) {
    // Shuffle the vector to randomize the order of elements
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(inputVector.begin(), inputVector.end(), g);
    // Select the first 'm' elements from the shuffled vector
    std::vector<T> result(inputVector.begin(), inputVector.begin() + std::min(m, static_cast<int>(inputVector.size())));
    return result;
  }

public:
  static void setup();

  Pass_randomize_dpins(const Eprp_var &var);

};
