
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <vector>

#include "sample_stage.hpp"

int pyrope_clock=0;

void worker_singlethread() {

  Sample_stage sample_stage;

  while(pyrope_clock<1000) {
    sample_stage.reset_cycle();
    pyrope_clock++;
  }

  while(pyrope_clock < 100000000) {
    sample_stage.cycle();
    pyrope_clock++;
  }
}

int main(int argc, char **argv) {

  printf("Starting simulation\n");

  worker_singlethread();

  printf("Simulation finished\n");

}
