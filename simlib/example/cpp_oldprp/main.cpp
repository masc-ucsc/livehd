
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <vector>

#include "Sample_Stage1.h"
#include "Sample_Stage2.h"
#include "Sample_Stage3.h"

//#define USE_PTHREAD_BARRIER 1

void usage(const char *name) {
  fprintf(stderr,"Usage:\n");
  fprintf(stderr,"\t%s [n threads]",name);
  exit(-2);
}

// NOTE: If workers can steal work from others, for load balance, this may be
// good to have it as lock free link list of tasks
std::vector<Stage *> w0; // worker zero
std::vector<Stage *> w1;
std::vector<Stage *> w2;

__thread Time_t pyrope_clock=0;
uint32_t nthreads = 1; // Single thread by default

void worker_singlethread() {

  uint32_t size = w0.size();

  // reset at the beginning for 1000 cycles (same as sample.v)
 
  while(pyrope_clock<1000) {
    for(uint32_t i=0;i<size;i++) {
      w0[i]->reset_cycle();
    }
    pyrope_clock++; 
    for(uint32_t i=0;i<size;i++) {
      w0[i]->update();
    }
  }

  while(pyrope_clock < 100000000) {
    for(uint32_t i=0;i<size;i++) {
      w0[i]->cycle();
    }
    pyrope_clock++;
    for(uint32_t i=0;i<size;i++) {
      w0[i]->update();
    }
  }
}

#ifdef USE_PTHREAD_BARRIER
pthread_barrier_t barrier;
#else
volatile uint32_t barrier1;
volatile uint32_t barrier2;
#endif


void *worker(void *arg) {

  uint32_t id = *(uint32_t *)arg;

  std::vector<Stage *> *w;
  if (id == 0) {
    w = &w0;
  }else if (id == 1) {
    w = &w1;
  }else if (id == 2) {
    w = &w2;
  }else{
    I(0);
    fprintf(stderr,"ERROR: Invalid id %d\n",id);
    fflush(stderr);
    exit(-3);
  }

  uint32_t size = w->size();

  // reset at the beginning for 1000 cycles (same as sample.v)
  printf("Starting thread %d..\n",id);
 
  while(pyrope_clock<1000) {
    for(uint32_t i=0;i<size;i++) {
      (*w)[i]->reset_cycle();
    }
    pyrope_clock++; 

#ifdef USE_PTHREAD_BARRIER
    pthread_barrier_wait(&barrier);
#else
    AtomicAdd(&barrier1,1);
    while(barrier1 !=nthreads) {
      ;
    }
    if( id == 0)
      AtomicSub(&barrier2,nthreads);
#endif

    for(uint32_t i=0;i<size;i++) {
      (*w)[i]->update();
    }

#ifdef USE_PTHREAD_BARRIER
    pthread_barrier_wait(&barrier);
#else
    AtomicAdd(&barrier2,1);
    while(barrier2 !=nthreads) {
      ;
    }
    if( id == 0)
      AtomicSub(&barrier1,nthreads);
#endif
  }

  while(pyrope_clock < 10000000) {
    for(uint32_t i=0;i<size;i++) {
      (*w)[i]->cycle();
    }
    pyrope_clock++;

#ifdef USE_PTHREAD_BARRIER
    pthread_barrier_wait(&barrier);
#else
    AtomicAdd(&barrier1,1);
    while(barrier1 !=nthreads) {
      ;
    }
    if( id == 0)
      AtomicSub(&barrier2,nthreads);
#endif

    for(uint32_t i=0;i<size;i++) {
      (*w)[i]->update();
    }

#ifdef USE_PTHREAD_BARRIER
    pthread_barrier_wait(&barrier);
#else
    AtomicAdd(&barrier2,1);
    while(barrier2 !=nthreads) {
      ;
    }
    if( id == 0)
      AtomicSub(&barrier1,nthreads);
#endif
  }

  return 0;
}


#define MAX_THREADS 128

pthread_t threads[MAX_THREADS];

int main(int argc, char **argv) {
  
  // check input paramters
  if (argc>1) {
    nthreads = atoi(argv[1]);
    if (nthreads <1 || nthreads >=MAX_THREADS) {
      fprintf(stderr,"ERROR: Invalid number of threads\n");
      usage(argv[0]);
    }
  }

  printf("Setting up simulation\n");

  // Create all the outputs

  Output_Sample_Stage1 *o1 = new Output_Sample_Stage1;
  Output_Sample_Stage2 *o2 = new Output_Sample_Stage2;
  Output_Sample_Stage3 *o3 = new Output_Sample_Stage3;

  // Create the stages

  Sample_Stage1 *s1 = new Sample_Stage1(o1, o2,o3);
  Sample_Stage2 *s2 = new Sample_Stage2(o2, o1);
  Sample_Stage3 *s3 = new Sample_Stage3(o3, o1,o2);

  printf("Starting simulation with %d threads\n", nthreads);

  if (nthreads == 1) {
    // assign work to workers
    w0.push_back(s1);
    w0.push_back(s2);
    w0.push_back(s3);

    worker_singlethread();
  }else if(nthreads == 3) {
#ifdef USE_PTHREAD_BARRIER
    int err = pthread_barrier_init(&barrier, 0, nthreads);
    if (err != 0) {
      fprintf(stderr,"ERROR: barrier init error = %d\n",err);
      exit(-3);
    }
#else
    barrier1 = 0;
    barrier2 = nthreads;
#endif
 
    // assign work to workers
    w0.push_back(s1);
    w1.push_back(s2);
    w2.push_back(s3);

    static uint32_t id[MAX_THREADS];

    for(uint32_t i = 0; i < nthreads; i++) {
      id[i] = i;
    }
    for(uint32_t i = 0; i < nthreads; i++) {
      pthread_create(&threads[i],NULL,worker,(void *)&id[i]);
    }

    for(uint32_t i = 0; i < nthreads; i++) {
      pthread_join(threads[i],NULL);
    }

#ifdef USE_PHTREAD_BARRIER
    pthread_barrier_destroy(&barrier);
#endif
  }

  printf("Simulation finished\n");

}
