#ifndef STAGE3_SAMPLE_H
#define STAGE3_SAMPLE_H

#include <vector>

#include "Snippets.h"

#include "Stage.h"

class Output_Sample_Stage1;
class Output_Sample_Stage2;

class Output_Sample_Stage3 {
public:
  uint32_t to1_b;
};

class Sample_Stage3 : public Stage {
protected:

  Output_Sample_Stage3 pcv;

  std::vector<uint32_t> memory; // TODO: build a generic sram_async class

  uint8_t reset_iterator; // local storage register
  uint32_t tmp; // local storage register
  uint32_t tmp2; // local storage register

  Output_Sample_Stage3 *output;

  Output_Sample_Stage1 *s1out;
  Output_Sample_Stage2 *s2out;

public:
  Sample_Stage3(Output_Sample_Stage3 *o, Output_Sample_Stage1 *s1out, Output_Sample_Stage2 *s2out);
  ~Sample_Stage3() {
  };

  void reset_cycle();
  void cycle();
  void update();
};

#endif

