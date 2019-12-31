// This program demonstrates how to use OpenTimer's API to
// run a simple timing-driven optimization loop plus
// incremental timing update.

// Design : rca.v
// SDC    : rca.sdc
// Library: osu018_stdcells.lib"(early/ min split)
// Library: osu018_stdcells.lib"(late / max split)

#include <ot/timer/timer.hpp>

int main(int argc, char *argv[]) {
  ot::Timer timer;

  // Read design
  timer.read_celllib("osu018_stdcells.lib", ot::MIN)
      .read_celllib("osu018_stdcells.lib", ot::MAX)
      //     .read_verilog("rca.v")
      .read_sdc("rca.sdc");

  // insert a buffer

  // timer.insert_gate("FA2", "FAX1")
  //     .insert_net("n1")
  //     .disconnect_pin("FA1:YC")
  //     .disconnect_pin("FAN:C")
  //     .connect_pin("FA1:YC", "n0")
  //     .connect_pin("FA2:C", "n0")
  //     .connect_pin("FA2:YC", "n1")
  //     .connect_pin("FAN:C", "n1");

  for (size_t i = 0; i < 100; ++i) {
    std::string a = std::to_string(i);
    std::string b = std::to_string(i + 1);
    std::string c = std::to_string(i - 1);

    timer.insert_gate("FA" + b, "FAX1")
        .insert_net("n" + a)
        .disconnect_pin("FA" + c + ":YC")
        .disconnect_pin("FAN:C")
        .connect_pin("FA" + c + ":YC", "n" + c)
        .connect_pin("FA" + b + ":C", "n" + c)
        .connect_pin("FA" + b + ":YC", "n" + i)
        .connect_pin("FAN:C", "n" + i);
  }

  // get the top-5 worst critical paths
  auto paths = timer.report_timing(5);

  for (size_t i = 0; i < paths.size(); ++i) {
    std::cout << "----- Critical Path " << i << " -----\n";
    std::cout << paths[i] << '\n';
  }

  // dump the timing graph to dot format for debugging
  timer.dump_graph(std::cout);

  return 0;
}
