
#include <unistd.h>

#include <vector>
#include <iostream>

#include "lgbench.hpp"
#include "dense.hpp"

#include "yas/serialize.hpp"
#include "yas/std_types.hpp"

void run_dense(int test_size) {

  LGBench b("dense.vector");

  size_t size=0;
  const char *filename = "array.dense.data";
  {
    // unnecessary? unlink(filename);
    Dense<int> array(filename);

    for(int i=0;i<test_size;i++) {
      array.emplace_back(i);
    }

    int total=0;
    for(int i=0;i<test_size;i+=237) {
      total += array[i];
    }

    std::cout << "dense.result:" << total << std::endl;

    size = array.size();

    b.sample("setup+serialize");
  }

  Dense<int> array2(filename);
  array2.reload(size);

  b.sample("unserialize");

  int total=0;
  for(int i=0;i<test_size;i+=237) {
    total += array2[i];
  }

  std::cout << "dense.check:" << total << std::endl;
}

void run_yas(int test_size) {

  LGBench b("yas.vector");

  std::vector<int> array;
  std::vector<int> array2;

  for(int i=0;i<test_size;i++) {
    array.push_back(i);
  }

  int total=0;
  for(int i=0;i<test_size;i+=237) {
    total += array[i];
  }

  std::cout << "ras.result:" << total << std::endl;

  b.sample("setup");

#ifdef MEMORY_ONLY_YAS
  constexpr std::size_t flags =
    yas::mem // IO type
    |yas::json; // IO format

  auto buf = yas::save<flags>(
      YAS_OBJECT("myobject", array)
      );

  yas::load<flags>(buf,
      YAS_OBJECT_NVP("myobject", ("array", array2))
      );
#else
  constexpr std::size_t flags =
    yas::file // IO type
    |yas::binary; // IO format

  const char *filename = "array.yas.data";
  unlink(filename);

  yas::save<flags>(filename,
      YAS_OBJECT("myobject", array)
      );

  b.sample("serialize");

  yas::load<flags>(filename,
      YAS_OBJECT("myobject", array2)
      );
#endif
  b.sample("unserialize");

  total=0;
  for(int i=0;i<test_size;i+=237) {
    total += array2[i];
  }

  std::cout << "yas.check:" << total << std::endl;
}

int main() {

  for(int i=0;i<32;i++) {
    run_yas(4000);
    run_dense(4000);
  }

  for(int i=0;i<4;i++) {
    run_yas(4000000);
    run_dense(4000000);
  }

  run_yas(60000000);
  run_dense(60000000);

  return 0;
}

