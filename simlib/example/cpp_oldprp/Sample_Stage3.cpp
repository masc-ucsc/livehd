
#include <stdlib.h>
#include <stdio.h>

#include "Sample_Stage3.h"

// Include the other stages that generate outputs used by this block
#include "Sample_Stage1.h"
#include "Sample_Stage2.h"

Sample_Stage3::Sample_Stage3(Output_Sample_Stage3 *o, Output_Sample_Stage1 *_s1out, Output_Sample_Stage2 *_s2out)
{
	output = o;
  s1out = _s1out;
  s2out = _s2out;

	reset_iterator = 0;

	memory.resize(256);
}

void Sample_Stage3::reset_cycle()
{
  tmp  = 0;
  tmp2 = 0;

	reset_iterator = reset_iterator + 1;
	memory[reset_iterator] = 0;
}

void Sample_Stage3::cycle()
{
	if ((tmp & 0xFFFF) == 45339) {
		if ((tmp2 &15) == 0) {
			printf("memory[127] = %ud\n",memory[127]);
		}
		tmp2 = tmp2 + 1;
	}

	pcv.to1_b = memory[tmp&0xff];

	if (s1out->to3_cValid && s2out->to3_dValid) {
		memory[(s1out->to3_c + tmp) & 0xff] = s2out->to3_d;
	}

	tmp = tmp + 7; // A prime number
}

void Sample_Stage3::update()
{
  *output = pcv;
}

