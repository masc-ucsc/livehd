#ifndef __libRW_H
#define __libRW_H

#include <graal_isolate.h>
#include <iostream>
#include <librapidwright.h>
using namespace std;

class graalThread {
private:
  graal_isolate_t *isolate = nullptr;
  graal_isolatethread_t *thread = nullptr;

public:
  graalThread();
  ~graalThread();
  int create_Design(char* designName);
  int create_FF(char* ff_name, int design_ID);
  int create_AND2(char* gate_name, int design_ID);
  void place_Cell(int, int);
  int set_IO_Buffer(bool boolean, int design_ID);
  void place_Design(int design_ID);
  void route_Design(int design_ID);
  void write_DCP(char* file_name, int design_ID);

};

graalThread::graalThread() {
  if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
    fprintf(stderr, "graal_create_isolate error\n");
    //return 1;
  }
}

int graalThread::create_Design(char* designName) {
  return RW_create_Design(thread, designName);
}

int graalThread::create_FF(char* ff_name, int design_ID) {
  return RW_create_FF(thread, ff_name, design_ID);
}

int graalThread::create_AND2(char* gate_name, int design_ID) {
  return RW_create_AND2(thread, gate_name, design_ID);
}

void graalThread::place_Cell(int cell_ID, int design_ID) {
  RW_place_Cell(thread, cell_ID, design_ID);
}

int graalThread::set_IO_Buffer(bool boolean, int design_ID) {
  return RW_set_IO_Buffer(thread, boolean, design_ID);
}

void graalThread::place_Design(int design_ID) {
  RW_place_Design(thread, design_ID);
}

void graalThread::route_Design(int design_ID) {
  RW_route_Design(thread, design_ID);
}

void graalThread::write_DCP(char* file_name, int design_ID) {
  RW_write_DCP(thread, file_name, design_ID);
}



#endif
