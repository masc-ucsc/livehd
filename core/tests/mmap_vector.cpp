
#include "dense.hpp"

#include <vector>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <boost/iostreams/device/mapped_file.hpp>


class potato{
public:
  int a;
  potato() {
    std::cerr << "called\n";
  }
  static void* operator new(std::size_t sz) {
    std::cout << "custom new for size " << sz << '\n';
    return ::operator new(sz);
  }
  static void* operator new[](std::size_t sz) {
    std::cout << "custom new for size " << sz << '\n';
    return ::operator new(sz);
  }
};
  std::ostream& operator<<(std::ostream& os, const potato& obj) {
    os << obj.a;
    return os;
  }

int main(int argc, char* argv[]) {
  std::chrono::system_clock::time_point begin_time =
    std::chrono::system_clock::now();

  Dense<potato> v("core_test_lgdb/rawvec");
  std::cout << "dump " << v.size() << " CODE1: ";
  for (size_t i=0;i<10;i++) {
    if (v.size() > i)
      std::cout << v[i].a << "  ";
    else
      std::cout << "X" << "  ";
  }
  std::cout << std::endl;

  if (argc!=1)
    v.clear();

  //potato *p1 = new potato();
  potato p;
  p.a = 65+v.size();
  v.emplace_back(p);
  if (v.size()>1) {
    v[0].a++;
  }
  std::cout << "dump " << v.size() << " CODE2: ";
  for (size_t i=0;i<10;i++) {
    if (v.size() > i)
      std::cout << v[i] << "  ";
    else
      std::cout << "X" << "  ";
  }
  std::cout << std::endl;

  std::chrono::system_clock::time_point end_time =
    std::chrono::system_clock::now();
  long long elapsed_miliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - begin_time).count();
  std::cout << "Duration (min:seg:mili): " << std::setw(2)
    << std::setfill('0') << (elapsed_miliseconds / 60000) << ":"
    << std::setw(2) << std::setfill('0')
    << ((elapsed_miliseconds / 1000) % 60) << ":" << std::setw(2)
    << std::setfill('0') << (elapsed_miliseconds % 1000) << std::endl;
  std::cout << "Total milliseconds: " << elapsed_miliseconds << std::endl;

  return 0;
}

// clang++ -Wall -std=c++11 mmap_vector.cpp -lboost_system -lboost_iostreams -lrt -lpthread
