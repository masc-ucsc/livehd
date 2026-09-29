// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_satopt.hpp"

#include "abc_lnet.hpp"
#include "abc_salt.hpp"

// clang-format off
extern "C" {
#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/cmd/cmd.h"
}
// clang-format on

namespace livehd::abc {

std::optional<std::vector<bool>> prove_const0(const synth::Lnet& net) {
  auto* previous = Abc_FrameReadGlobalFrame();
  auto* frame    = Abc_FrameCreate();
  if (!frame) {
    return std::nullopt;
  }
  Abc_FrameEnter(frame);
  const auto        outputs = net.outputs().size();
  std::vector<bool> proven(outputs, false);
  auto*             ntk = static_cast<Abc_Ntk_t*>(lnet_to_abc(net, "satopt", Lnet_names::proof));
  if (outputs != 0) {
    Abc_NtkAddDummyPiNames(ntk);
    Abc_NtkAddDummyPoNames(ntk);
    Abc_Ntk_t* logic = Abc_NtkToLogic(ntk);
    if (logic) {
      Abc_FrameReplaceCurrentNetwork(frame, logic);
      if (Cmd_CommandExecute(frame, "strash; &get -n; &fraig -x -C 500; &put; strash") == 0) {
        auto* swept = Abc_FrameReadNtk(frame);
        if (Abc_NtkPoNum(swept) == static_cast<int>(outputs)) {
          for (size_t i = 0; i < outputs; ++i) {
            auto* po = Abc_NtkPo(swept, static_cast<int>(i));
            if (Abc_ObjFanin0(po) == Abc_AigConst1(swept) && Abc_ObjFaninC0(po)) {
              proven[i] = true;
            }
          }
        }
      }
    }
  }
  Abc_NtkDelete(ntk);
  Abc_FrameLeave(previous);
  Abc_FrameDestroy(frame);
  return proven;
}

namespace {
const livehd::satopt::Mux_prover& abc_prover() {
  static const livehd::satopt::Mux_prover prover{prove_const0, kAbcSrcSalt};
  return prover;
}
// pass.satopt (ABC-free) proves mux facts with ABC when this library is linked.
[[maybe_unused]] const bool registered = (livehd::satopt::register_mux_prover(abc_prover()), true);
}  // namespace

std::shared_ptr<const Satopt_result> satopt(hhds::Graph* graph, std::string_view cache_dir, bool all_regions) {
  return livehd::satopt::mux_satopt(graph, abc_prover(), cache_dir, all_regions);
}

Mux_satopt optimize_muxes(hhds::Graph* graph, std::string_view cache_dir, bool all_regions) {
  return livehd::satopt::optimize_muxes(graph, abc_prover(), cache_dir, all_regions);
}

}  // namespace livehd::abc
