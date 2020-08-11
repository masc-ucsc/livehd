
#include <stdio.h>
#include <stdlib.h>

#include <cstring>

#include "lbench.hpp"

char *mem;

int main() {

  mem = (char *)malloc(1024*1024*10);

  int pos =0;
  for(int i=0;i<1024*1024*10;++i) {
    mem[i]='a'; // it can not have zeroes
  }

  char cadena[1000];

  for(int i=0;i<1024*1024*10;i = i + 20) {
    if (i>7000000)
      sprintf(cadena,"hello%d",i);
    else
      sprintf(cadena,"potatoes");
    for(int j=0;j<strlen(cadena)-1;++j) {
      mem[i+j] = cadena[j];
    }
    if (strcmp(&mem[i],"hello1000000")==0) {
      pos = i;
      printf("created\n");
    }
  }
  if (strcmp(&mem[pos],"hello1000000")==0) {
    printf("created2 [pos]\n");
  }

  {
    Lbench s("not found");
    int conta=0;

    for(int i=0;i<100;++i) {
      const char *p1;
      if (i&3)
        p1 = std::strstr(mem,"nothere");
      else
        p1 = std::strstr(mem,"nothere_at_all");
      if (p1==nullptr)
        conta++;
    }
    printf("p1 %d\n",conta);
  }
  {
    Lbench s("found end");
    int conta=0;
    for(int i=0;i<100;++i) {
      const char *p1;
      if (i&3)
        p1 = std::strstr(mem,"hello1000000");
      else
        p1 = std::strstr(mem,"hello1000020");
      if (p1==nullptr)
        conta++;
    }
    printf("p1 %d\n",conta);
  }
}
