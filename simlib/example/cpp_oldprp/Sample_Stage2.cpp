
#include <stdlib.h>

#include "Sample_Stage2.h"

// Include the other stages that generate outputs used by this block
#include "Sample_Stage1.h"

Sample_Stage2::Sample_Stage2(Output_Sample_Stage2 *o, Output_Sample_Stage1 *_s1out)
{
	output = o;
  s1out = _s1out;
}

void Sample_Stage2::reset_cycle()
{
  tmp = 1;

  pcv.to3_dValid = false;

  pcv.to2_eValid = false;

  pcv.to1_aValid = false;
}

void Sample_Stage2::cycle()
{
	pcv.to3_dValid =  (tmp & 1) == 0;
	pcv.to3_d = tmp+s1out->to2_b;

	pcv.to2_eValid =  (tmp & 1) == 1 && s1out->to2_aValid && pcv.to1_aValid;
	pcv.to2_e = tmp+s1out->to2_a + pcv.to1_a;

	pcv.to1_aValid =  (tmp & 2) == 2;
	pcv.to1_a = tmp+3;

	tmp = tmp + 13; // A prime number
}

void Sample_Stage2::update()
{
  *output = pcv;
}

