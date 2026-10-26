// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timing_model.hpp"

#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace nldm = livehd::liberty::timing;
namespace {
const std::string model = R"lib(
library(test) {
  time_unit : "1ns";
  capacitive_load_unit(1,pf);
  default_input_pin_cap : 0.002;
  default_max_transition : .5;
  slew_lower_threshold_pct_rise : 10;
  slew_upper_threshold_pct_rise : 90;
  slew_derate_from_library : .8;
  lu_table_template(normal) {
    variable_1 : input_net_transition;
    variable_2 : total_output_net_capacitance;
    index_1(".01,.03"); index_2(".002,.006");
  }
  lu_table_template(swapped) {
    variable_1 : total_output_net_capacitance;
    variable_2 : input_net_transition;
    index_1(".002,.006"); index_2(".01,.03");
  }
  lu_table_template(setup) {
    variable_1 : constrained_pin_transition;
    variable_2 : related_pin_transition;
    index_1(".01,.03"); index_2(".02,.04");
  }
  cell(INV) {
    area : 3;
    pin(A) {direction : input; capacitance : .003; rise_capacitance : .004;}
    pin(EN) {direction : input;}
    pin(Y) {
      direction : output; function : "!A"; max_capacitance : .1;
      timing() {
        related_pin : A; timing_sense : negative_unate; when : "EN";
        cell_rise(normal) { values(".01,.03", ".03,.05"); }
        cell_fall(swapped) { values(".02,.04", ".04,.06"); }
        rise_transition(normal) {values(".02,.04", ".04,.06");}
        fall_transition(swapped) {values(".03,.05", ".05,.07");}
      }
    }
  }
  cell(FF) {
    ff(IQ, IQN) {clocked_on : "CLK"; next_state : "D";}
    pin(CLK) {direction : input; clock : true;}
    pin(D) {direction : input;
      timing() {related_pin : CLK; timing_type : setup_rising;
        rise_constraint(setup) { values("-.01,.01", ".01,.03"); }
        fall_constraint(setup) { values("0,.02", ".02,.04"); }
      }
    }
    pin(Q) {direction : output;
      timing() {related_pin : CLK; timing_type : rising_edge; timing_sense : non_unate;
        cell_rise(scalar) {values(".03");}
        cell_fall(scalar) {values(".04");}
        rise_transition(scalar) {values(".05");}
        fall_transition(scalar) {values(".06");}
      }
    }
  }
})lib";
}  // namespace

TEST(TimingModel, UnitsAxisOrderAndInterpolation) {
  const auto  library = nldm::parse(model);
  const auto& cell    = library.cells.at("INV");
  const auto& arc     = cell.pins.at("Y").arcs.at(0);
  ASSERT_TRUE(arc.unsupported.empty());
  EXPECT_EQ(arc.sense, nldm::Sense::negative);
  EXPECT_EQ(arc.when, "EN");
  nldm::Query query{.input_slew_ps = 20, .output_load_ff = 4};
  EXPECT_NEAR(arc.delay[0]->lookup(query), 30, 1e-12);
  EXPECT_NEAR(arc.delay[1]->lookup(query), 40, 1e-12);
  EXPECT_NEAR(arc.slew[0]->lookup(query), 40, 1e-12);
  EXPECT_NEAR(arc.slew[1]->lookup(query), 50, 1e-12);
  EXPECT_EQ(cell.pins.at("A").capacitance_ff[0], 4);
  EXPECT_EQ(cell.pins.at("A").capacitance_ff[1], 3);
  EXPECT_EQ(cell.pins.at("EN").capacitance_ff[0], 2);
  EXPECT_EQ(cell.pins.at("Y").max_capacitance_ff, 100);
  EXPECT_EQ(cell.pins.at("Y").max_transition_ps, 500);
  EXPECT_EQ(cell.thresholds.lower[0], .1);
  EXPECT_EQ(cell.thresholds.upper[0], .9);
  EXPECT_EQ(cell.thresholds.slew_derate, .8);
}
TEST(TimingModel, ExtrapolatesUsingBoundaryIntervals) {
  const auto  library = nldm::parse(model);
  const auto& table   = *library.cells.at("INV").pins.at("Y").arcs.at(0).delay[0];
  EXPECT_NEAR(table.lookup({.input_slew_ps = 0, .output_load_ff = 0}), -10, 1e-12);
  EXPECT_NEAR(table.lookup({.input_slew_ps = 50, .output_load_ff = 10}), 90, 1e-12);
  EXPECT_THROW(table.lookup({.input_slew_ps = -1}), std::invalid_argument);
  EXPECT_THROW(table.lookup({.input_slew_ps = std::numeric_limits<double>::infinity()}), std::invalid_argument);
}
TEST(TimingModel, OutputLoadProjectionPreservesBothAxisOrdersAndBoundaryExtrapolation) {
  const auto  library = nldm::parse(model);
  const auto& arc     = library.cells.at("INV").pins.at("Y").arcs.at(0);
  for (const auto* table : {&*arc.delay[0], &*arc.delay[1]}) {
    for (double input : {0., 10., 20., 30., 50.}) {
      const auto slice = table->output_load_slice({.input_slew_ps = input});
      EXPECT_EQ(slice.x_variable, nldm::Variable::output_load);
      EXPECT_EQ(slice.y_variable, nldm::Variable::none);
      for (double cap : {0., 2., 4., 6., 10.}) {
        EXPECT_NEAR(slice.lookup({.output_load_ff = cap}), table->lookup({.input_slew_ps = input, .output_load_ff = cap}), 1e-12);
      }
    }
  }
  const auto& scalar = *library.cells.at("FF").pins.at("Q").arcs[0].delay[0];
  EXPECT_EQ(scalar.output_load_slice({}).lookup({.output_load_ff = 100}), 30);
  nldm::Table input_only{
      nldm::Variable::input_slew,
      nldm::Variable::none,
      {1, 3},
      {},
      {2, 6}
  };
  EXPECT_EQ(input_only.output_load_slice({.input_slew_ps = 2}).lookup({.output_load_ff = 100}), 4);
}
TEST(TimingModel, ConstraintAxesAndClockToQRemainDistinct) {
  const auto  library = nldm::parse(model);
  const auto& cell    = library.cells.at("FF");
  EXPECT_TRUE(cell.sequential);
  EXPECT_FALSE(cell.latch);
  EXPECT_TRUE(cell.pins.at("CLK").clock);
  const auto& setup = cell.pins.at("D").arcs.at(0);
  EXPECT_EQ(setup.type, "setup_rising");
  EXPECT_NEAR(setup.constraint[0]->lookup({.related_slew_ps = 30, .constrained_slew_ps = 20}), 10, 1e-12);
  const auto& clock = cell.pins.at("Q").arcs.at(0);
  EXPECT_EQ(clock.type, "rising_edge");
  EXPECT_EQ(clock.delay[0]->lookup({}), 30);
  EXPECT_EQ(clock.delay[1]->lookup({}), 40);
}
TEST(TimingModel, IndependentLibraryUnitsAndTemplates) {
  const auto  library = nldm::parse(model + R"(
    library(other) {time_unit:"10ps"; capacitive_load_unit(2,ff);
      lu_table_template(normal) {variable_1:total_output_net_capacitance;index_1("1,3");}
      cell(BUF) {pin(Y) {direction:output;timing(){related_pin:A;
        cell_rise(normal) {values("2,6");}
      }}}
    })");
  const auto& table   = *library.cells.at("BUF").pins.at("Y").arcs[0].delay[0];
  EXPECT_EQ(table.lookup({.output_load_ff = 4}), 40);
  EXPECT_EQ(library.cells.at("INV").pins.at("Y").arcs[0].delay[0]->lookup({.input_slew_ps = 20, .output_load_ff = 4}), 30);
}
TEST(TimingModel, BusTimingAndPerBitPinLoadAreExpanded) {
  const auto  library = nldm::parse(R"(
    library(ram) {time_unit:"1ns"; capacitive_load_unit(1,pf);
      type(DATA) {bit_width:4;bit_from:3;bit_to:0;}
      lu_table_template(load) {variable_1:total_output_net_capacitance;index_1(".001,.003");}
      cell(RAM) {memory() {type:ram;}
        bus(D) {bus_type:DATA;direction:input;capacitance:.002;}
        bus(Q) {bus_type:DATA;direction:output;
          timing() {related_pin:CLK;timing_type:rising_edge;
            cell_rise(load){values(".01,.03")}
          }
        }
      }
    })");
  const auto& cell    = library.cells.at("RAM");
  EXPECT_TRUE(cell.memory);
  EXPECT_TRUE(cell.sequential);
  ASSERT_EQ(cell.pins.size(), 8);
  EXPECT_TRUE(cell.unsupported.empty());
  EXPECT_EQ(cell.pins.at("D[3]").capacitance_ff[0], 2);
  EXPECT_NEAR(cell.pins.at("Q[0]").arcs[0].delay[0]->lookup({.output_load_ff = 2}), 20, 1e-12);
}
TEST(TimingModel, CommentsQuotedPunctuationAndLineContinuations) {
  const auto  library = nldm::parse(R"(
    /* library(fake) { */ library("real") {
      comment:"/* not a comment */ and } and \"quoted\"";
      time_unit:"1ps"; // not ns
      lu_table_template(t) {variable_1:input_net_transition;index_1("1,2");}
      cell(X) {pin(Y) {direction:output;
        timing() {related_pin:"A B";when:"A' & B";
          cell_rise(t) {values(\
            "2,4"\
          )}
        }
      }}
    })");
  const auto& arcs    = library.cells.at("X").pins.at("Y").arcs;
  ASSERT_EQ(arcs.size(), 2);
  EXPECT_EQ(arcs[0].related_pin, "A");
  EXPECT_EQ(arcs[1].related_pin, "B");
  EXPECT_EQ(arcs[0].when, "A' & B");
  EXPECT_EQ(arcs[0].delay[0]->lookup({.input_slew_ps = 1.5}), 3);
}
TEST(TimingModel, InvalidSyntaxUnitsAndDimensionsAreRejected) {
  for (const auto& text : {"library(x){",
                           "library(x){/* open",
                           "library(x){time_unit:\"bad\";}",
                           "library(x){capacitive_load_unit(1,unknown);}",
                           "library(x){slew_lower_threshold_pct_rise:90;slew_upper_threshold_pct_rise:10;}",
                           "library(x){cell(X){pin(A){direction:input;capacitance:-1;}}}",
                           "library(x){cell(X){pin(Y){timing(){cell_rise(scalar){values(\"1,2\");}}}}}"}) {
    EXPECT_THROW(nldm::parse(text), std::invalid_argument) << text;
  }
  EXPECT_THROW(nldm::parse("library(a){cell(X){}} library(b){cell(X){}}"), std::invalid_argument);
}
TEST(TimingModel, UnsupportedTablesStayExplicitAndDoNotBecomeZeroDelay) {
  const auto library = nldm::parse(R"(
    library(x) {
      lu_table_template(ccs) {variable_1:input_net_transition;variable_2:normalized_voltage;index_1("1,2");index_2("0,1");}
      cell(X) {pin(Y) {direction:output;timing(){cell_rise(ccs){values("1,2","3,4");}}}}
      cell(Z) {pin(Y) {direction:output;timing(){cell_rise(missing){values("1");}}}}
    })");
  for (const auto& name : {"X", "Z"}) {
    const auto& arc = library.cells.at(name).pins.at("Y").arcs[0];
    EXPECT_FALSE(arc.unsupported.empty());
    EXPECT_FALSE(arc.delay[0]);
  }
}
TEST(TimingModel, SinglePointAxesAreConstantAndUncharacterizedCapsStayMissing) {
  const auto  library = nldm::parse(R"(
    library(x) {time_unit:"1ps";
      lu_table_template(t){variable_1:input_net_transition;index_1("1");}
      cell(X){pin(A){direction:input;} pin(Y){direction:output;
        timing(){cell_rise(t){values("9");}}
      }}
    })");
  const auto& cell    = library.cells.at("X");
  EXPECT_FALSE(cell.pins.at("A").capacitance_ff[0]);
  EXPECT_EQ(cell.pins.at("Y").arcs[0].delay[0]->lookup({.input_slew_ps = 500}), 9);
}

TEST(TimingModel, OmittedScalarSemicolonDoesNotConsumeTheFollowingGroup) {
  const auto  library = nldm::parse(R"(
    library(x) {time_unit:"1ps";
      cell(BUF) {area:0.25
        pg_pin(VDD){pg_type:primary_power;}
        pin(A){direction:input;capacitance:2;}
        pin(Y){direction:output;timing(){related_pin:A;timing_sense:positive_unate;
          cell_rise(scalar){values("9")}
        }}
      }
    })");
  const auto& cell    = library.cells.at("BUF");
  EXPECT_EQ(cell.area, .25);
  EXPECT_EQ(cell.pins.at("A").capacitance_ff[0], 2000);
  EXPECT_EQ(cell.pins.at("Y").arcs[0].delay[0]->lookup({}), 9);
}

TEST(TimingModel, ExplicitBusMembersInheritAndOverrideWithoutInventingExtraBits) {
  const auto  library = nldm::parse(R"(
    library(x){time_unit:"1ps";capacitive_load_unit(1,ff);
      type(wide){bit_width:128;bit_from:127;bit_to:0;}
      cell(PHY){
        bus(D){bus_type:wide;direction:input;capacitance:2;
          pin(D[0]){rise_capacitance:3;}
          pin(D[7]){capacitance:4;}
        }
        bus(Q){bus_type:wide;direction:output;function:"1";
          timing(){related_pin:CLK;timing_type:rising_edge;cell_rise(scalar){values("10");}}
          pin(Q[0]){function:"0";
            timing(){related_pin:CLK;timing_type:rising_edge;cell_rise(scalar){values("20");}}
          }
          pin(Q[3]){}
        }
      }
    })");
  const auto& cell    = library.cells.at("PHY");
  EXPECT_TRUE(cell.unsupported.empty());
  ASSERT_EQ(cell.pins.size(), 4);
  EXPECT_EQ(cell.pins.at("D[0]").capacitance_ff[0], 3);
  EXPECT_EQ(cell.pins.at("D[0]").capacitance_ff[1], 2);
  EXPECT_EQ(cell.pins.at("Q[0]").function, "0");
  EXPECT_EQ(cell.pins.at("Q[3]").function, "1");
  ASSERT_EQ(cell.pins.at("Q[0]").arcs.size(), 1);
  EXPECT_EQ(cell.pins.at("Q[0]").arcs[0].delay[0]->lookup({}), 20);
  EXPECT_EQ(cell.pins.at("Q[3]").arcs[0].delay[0]->lookup({}), 10);
}
