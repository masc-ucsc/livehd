#pragma once

struct Sample2_stage {
  UInt<1>     to1_aValid;
  UInt<32> to1_a;

  UInt<1>     to2_eValid;
  UInt<32> to2_e;

  UInt<1>     to3_dValid;
  UInt<32> to3_d;

  UInt<32> tmp;

  void reset_cycle();
  void cycle(UInt<1> s1_to2_aValid, UInt<32> s1_to2_a, UInt<32> s1_to2_b) {
    to3_dValid =  !(tmp.bit<0>());
    to3_d = tmp.addw(s1_to2_b);

    to2_eValid =  tmp.bit<0>() && s1_to2_aValid && to1_aValid;
    UInt<32> tmp3 = tmp.addw(s1_to2_a);

    to2_e = tmp3.addw(to1_a);

    //to1_aValid =  (tmp & UInt<32>(2)) == UInt<32>(2);
    to1_aValid =  tmp.bit<1>();
    to1_a = tmp.addw(UInt<32>(3));

    tmp = tmp.addw(UInt<32>(13));
  }
};

