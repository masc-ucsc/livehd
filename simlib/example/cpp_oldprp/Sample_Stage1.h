#ifndef STAGE1_SAMPLE_H
#define STAGE1_SAMPLE_H

#include "Snippets.h"

#include "Stage.h"

class Output_Sample_Stage2;
class Output_Sample_Stage3;

class Output_Sample_Stage1 {
public:
  bool to2_aValid;
  uint32_t to2_a;
  uint32_t to2_b;

  bool to3_cValid;
  uint32_t to3_c;
};

class Sample_Stage1 : public Stage {
protected:

  Output_Sample_Stage1 pcv;

  uint32_t tmp; // local storage register

  Output_Sample_Stage1 *output;

  Output_Sample_Stage2 *s2out;
  Output_Sample_Stage3 *s3out;

public:
  Sample_Stage1(Output_Sample_Stage1 *o, Output_Sample_Stage2 *s2out, Output_Sample_Stage3 *s3out);
  ~Sample_Stage1() {
  };

  void reset_cycle();
  void cycle();
  void update();
};

#endif

