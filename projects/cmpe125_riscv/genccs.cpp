#include <sys/stat.h>

#include <fstream>
using namespace std;

int main(int argc, char *argv[]) {
  mkdir(argv[1], S_IRWXU);
  ofstream myfile;
  myfile.open(string(argv[1]) + string("/foo.cpp"));
  myfile << "// " << string(argv[1]) << string("/foo.cpp") << std::endl;
  myfile << "int toto() { return 42; } ";
  return 0;
}
