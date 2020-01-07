
#include <stdlib.h>

#include "Sample_Stage1.h"

// Include the other stages that generate outputs used by this block
#include "Sample_Stage2.h"
#include "Sample_Stage3.h"

Sample_Stage1::Sample_Stage1(Output_Sample_Stage1 *o, Output_Sample_Stage2 *_s2out, Output_Sample_Stage3 *_s3out)
{
  output = o;
  s3out = _s3out;
  s2out = _s2out;
}

void Sample_Stage1::reset_cycle()
{
  tmp = 0;
  pcv.to2_aValid = false;
  pcv.to3_cValid = false;
  pcv.to3_c = 0;
}

void Sample_Stage1::cycle()
{
  pcv.to2_b = s3out->to1_b + 1;

  pcv.to2_a      = s2out->to1_a + s3out->to1_b + 2;
  pcv.to2_aValid = s2out->to1_aValid;

  pcv.to3_cValid =  (tmp & 1) != 0;
  pcv.to3_c = tmp + s2out->to1_a;

  tmp = tmp + 23; // FIXME: The tmp or register should use the update too
}

void Sample_Stage1::update()
{
  *output = pcv;
}

