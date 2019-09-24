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
  int create_Design(char* designName);
  void create_FF(char* ff_name, int id);
  int set_IO_Buffer(bool boolean, int id);
  void place_Design(int id);
  void write_DCP(char* file_name, int id);


};

graalThread::graalThread() {
  if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
    fprintf(stderr, "graal_create_isolate error\n");
    //return 1;
  }
}

int graalThread::create_Design(char* designName) {
  return RW_Create_Design(thread, designName);
}

void graalThread::create_FF(char* ff_name, int id) {
  RW_Create_FF(thread, ff_name, id);
}

void graalThread::RW_Create_AND2(char* gate_name, int id) {
  RW_Create_AND2(thread, gate_name, id);
}

int graalThread::set_IO_Buffer(bool boolean, int id) {
  return RW_set_IO_Buffer(thread, boolean, id);
}

void graalThread::place_Design(int id) {
  RW_place_Design(thread, id);
}

void graalThread::write_DCP(char* file_name, int id) {
  RW_write_DCP(thread, file_name, id);
}



#endif
