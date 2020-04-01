//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include <cassert>
#include "lbench.hpp"
#include "likely.hpp"
#include "simlib_signature.hpp"

#include <vector>
#include <iterator>

template<typename Top_struct>
class Simlib_checkpoint {
  uint64_t ncycles;
  int checkpoint_ncycles;
  int next_checkpoint_ncycles;
  double last_checkpoint_sec;
  uint64_t reset_ncycles;
  const std::string name;
  std::string path; // checkpoint enabled/path

  Top_struct top;
  Simlib_signature signature;

  Lbench perf;

  void advance_reset(uint64_t n=1) {
    for(auto i=0;i<n;++i) {
      top.cycle();
      top.reset_cycle();
    }
    ncycles += n;
  };

public:
  Simlib_checkpoint(std::string_view _name, uint64_t _reset_ncycles = 10000) : name(_name), top(0), perf(name), reset_ncycles(_reset_ncycles) {
    ncycles = 0;
    checkpoint_ncycles = -1; // Disable checkpoint by default
    next_checkpoint_ncycles = 1000000000;
    last_checkpoint_sec = 0.0;

    advance_reset(reset_ncycles);

    top.add_signature(signature);
    //std::cout<<"name: "<<name<<"\n top: "<<top.hidx<<"\n"<<"---"<<"\n" ;
  };

  ~Simlib_checkpoint() {
    std::string ext;
    double speed = static_cast<double>(ncycles)/perf.get_secs();
    if (speed>1e6) {
      speed /= 1e6;
      ext = "MHz";
    }else if (speed>1e3) {
      speed /= 1e3;
      ext = "KHz";
    }else{
      ext = "Hz";
    }
    fprintf(stderr, "simlib: simulation finished with %lld cycles (%.2f%s)\n", (long)ncycles, (float)speed, ext.c_str());
  }

  void set_checkpoint_cycles(int n) {
    if (path.empty())
      n = 1000000000;

    n >>= 10;
    n <<= 10;
    if (n<1024)
      n = 1024;
    checkpoint_ncycles = n;

    next_checkpoint_ncycles = ncycles + checkpoint_ncycles;
  }

  size_t calc_bytes() const {
    return sizeof(top) + signature.get_map_bytes();
  }

  void enable_trace(std::string_view _path) {
    path = _path;
    if (access(path.c_str(), W_OK)==-1) {
      fprintf(stderr,"simlib: ERROR unable to access path:%s\n",path.c_str());
      exit(3);
    }
    const int pages = (calc_bytes()>>12)+1;


    set_checkpoint_cycles(10000/pages);
  }

  static bool str_ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
  }

  static bool str_starts_with(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
  }

  uint64_t find_previous_checkpoint(uint64_t cycles) {
    DIR *dr = opendir(path.c_str());
    assert(dr != nullptr);

    struct dirent *de;  // Pointer for directory entry

    uint64_t best_found = 0; // reset everything

    while ((de = readdir(dr)) != NULL) {
      const std::string d_name{de->d_name};

      if (!str_starts_with(d_name, name))
        continue;
      if (!str_ends_with(d_name, ".ckpt"))
        continue;

      const std::string str_cycles = d_name.substr(name.size()+1, d_name.size()-5); // 5 for .ckpt
      auto val = std::atoi(str_cycles.c_str());

      if (val > best_found && val <= cycles)
        best_found = val;
    }
    closedir(dr);

    return best_found;
  }

  bool load_checkpoint(uint64_t cycles) {
    printf("load checkpoint @%lld\n", cycles);
    std::string filename = path + "/" + name + "_" + std::to_string(cycles) + ".ckpt";
    //std::cout<<"filename is :"<<filename<<std::endl;//SG
    int fd = ::open(filename.c_str(), O_RDONLY, 0644);
    //0644 file system permission flags : it stands for -rw-r--r-- file permission.
    if (fd <0) {
      //fd<0 means the file was not opened/not found for opening/error while opening.
      return false;
    }

    Simlib_signature s2(signature);
    auto sz = read(fd, s2.get_map_address(), s2.get_map_bytes());
    if (sz!=signature.get_map_bytes()) {
      fprintf(stderr,"simlib: ERROR corrupted checkpoint:%s signature loading\n",filename.c_str());
      exit(3);
    }
    if (s2 != signature) {
      printf("missmatch signature load checkpoint @%lld\n", cycles);
      return false;
    }

    auto sz2 = read(fd, &top, sizeof(top));
    if (sz2!=sizeof(top)) {
      fprintf(stderr,"simlib: ERROR corrupted checkpoint:%s data loading\n",filename.c_str());
      exit(3);
    }

    close(fd);

    return true;
  }

  bool load_intermediate_checkpoint(uint64_t cycles){
   printf("load intermediate checkpoint @%lld\n", cycles);
   std::vector<int> myvector;
    DIR *dr = opendir(path.c_str());
   if (dr ==NULL) {
     fprintf(stderr,"simlib: ERROR unable to access path:%s\n", path.c_str());
     exit(-1);
   }
   struct dirent *de;
   std::string match = "check_";
   while ((de = readdir(dr)) != NULL) {
     std::string chop_name(de->d_name);
     if (chop_name.substr(0,6)==match) {
         int mydata = atoi(chop_name.substr(6,chop_name.size()-11).c_str());
         myvector.push_back(mydata);
     }
   }
    for (int i=0;i<myvector.size();i++) {
      if(cycles==myvector[i]) {
        load_checkpoint(cycles);
        return true;
      }
    }

    std::sort (myvector.begin(), myvector.end());
    std::vector<int>::iterator low;
    low=std::lower_bound (myvector.begin(), myvector.end(), cycles);
    int lower_cycles = myvector[low- myvector.begin()-1] ;
    if(lower_cycles==0) {
      advance_reset(reset_ncycles);
      advance_clock(cycles-reset_ncycles);
      return true;}
    load_checkpoint(lower_cycles);
    advance_clock(cycles-lower_cycles);
    return true;
  }
  void save_checkpoint() {
    printf("Save checkpoint @%lld\n", ncycles);
    std::string filename = path + "/" + name + "_" + std::to_string(ncycles) + ".ckpt";

    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd <0) {
      fprintf(stderr,"simlib: ERROR unable to create checkpoint:%s\n",filename.c_str());
      exit(3);
    }
    int ret = ::ftruncate(fd, calc_bytes());
    if (ret<0) {
      fprintf(stderr,"simlib: ERROR unable to grown checkpoint:%s\n",filename.c_str());
      exit(3);
    }
    uint8_t *base = (uint8_t *)::mmap(nullptr, calc_bytes(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (base == MAP_FAILED) {
      fprintf(stderr,"simlib: ERROR unable to mmap checkpoint:%s\n",filename.c_str());
      exit(3);
    }

    ::memcpy(base, signature.get_map_address(), signature.get_map_bytes());
    ::memcpy(&base[signature.get_map_bytes()], &top, sizeof(top));
    ::munmap(base, calc_bytes());
    close(fd);
  }

  void handle_checkpoint() {
    //std::cout<<"in handle"<<std::endl;
    auto delta_secs = perf.get_secs()-last_checkpoint_sec;
    if (true || delta_secs>0.4)
      save_checkpoint();
    else
      last_checkpoint_sec = perf.get_secs();

    if (delta_secs<1) {
      set_checkpoint_cycles(4*checkpoint_ncycles);
    }else if (delta_secs<2) {
      set_checkpoint_cycles(1.5*checkpoint_ncycles);
    }else if (delta_secs>4) {
      set_checkpoint_cycles(checkpoint_ncycles/4);
    }else{
      set_checkpoint_cycles(checkpoint_ncycles/1.5);
    }
  }

  void advance_clock(uint64_t n=1) {
    do {
      int step = n;
      if (step> next_checkpoint_ncycles)
        step = next_checkpoint_ncycles;

      n -= step;//SG: if step==n then this line will make n=0 thus making the possibility of n>0 unlikely.
      next_checkpoint_ncycles -= step;
      for(auto i=0;i<step;++i)
        top.cycle();
      ncycles += step;

      if (unlikely(next_checkpoint_ncycles<=0)) {
        handle_checkpoint();
      }
    }while(unlikely(n>0));
  };

};

