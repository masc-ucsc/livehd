#include <fstream>
#include <sys/stat.h>
using namespace std;

int main (int argc, char *argv[]) {
  mkdir(argv[1], S_IRWXU);
  ofstream myfile;
  myfile.open(string(argv[1]) + string("/foo.cpp"));
  myfile << "// " << string(argv[1]) << string("/foo.cpp") << std::endl;
  myfile << "int toto() { return 42; } ";
  return 0;
}

