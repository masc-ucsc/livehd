#include <stdio.h>
#include <stdlib.h>

size_t round_power2(int num) {
  if (num<=8) {
    return 8;
  }

  num = (num-1)>>2;
  int clz = __builtin_clz(num);
  return 1<<(34-clz);
}

int main(int argc, char **argv) {

  if (argc!=2) {
    printf("Usage:\n\t%s <num>\n",argv[0]);
    exit(-1);
  }

  int16_t  num = atoi(argv[1]);
  int ctz = 0; 
  int ones = 0;

  int clz = __builtin_clz(num);
  printf("size is %d %d (%d)\n", round_power2(num), clz, 1<<(32-clz-1));  

  return 0;
}
