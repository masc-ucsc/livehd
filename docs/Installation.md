# Installation

This is a high level description of how to build LiveHD.

## Requirements

Although LiveHD should run on most common Linux distributions, it is heavily tested on both Arch and Kali (Debian based).

The following programs are assumed to be present when building LiveHD:
 - GCC 8+ or Clang 8+ (C++17 support is required)
 - Yosys
 - Bazel

The following programs are optional:
 - pandoc (for better viewing of markdown documentation)

It is also assumed that bash is used to compile LiveHD.

gcc and clang offers better warnings and execution speed dependent of the benchmark.

If you're unsure if your copy of gcc or clang is new enough, you can check the version by typing 
```
  g++ --version
```
  or
```
  clang++ --version
```  

LiveHD requires a specific commit of Yosys which needs to be built from source.

## Steps

0. Make sure that you do not have another version installed in your system
  ```
      which -a yosys
  ```

1. **Download LiveHD source**  
  ```
    git clone https://github.com/masc-ucsc/livehd
  ```
2. **Build Yosys from source**  
  LiveHD requires a specific commit of Yosys in order to function properly.  Versions of Yosys installed through apt, pacman, etc. will not work.  
  When building Yosys from source, it will pull in additional dependencies it needs.  Check https://github.com/YosysHQ/yosys/#setup for more information.

  - Download the Yosys source (the exact directory doesn't matter as long as it's not inside LiveHD)  
      ```
         git clone https://github.com/YosysHQ/yosys
         cd yosys
      ```
  - Find the Yosys commit LiveHD uses and check out that commit (Version is in the WORKSPACE file in LiveHD)
      ```
         git checkout `$ grep -C2 BUILD.yosys <LiveHD_path>/WORKSPACE | grep commit | cut -d\" -f2`
      ```
  - Tell Yosys the compiler we want to use, build, and install yosys
      ```
         make config-gcc``` (if using gcc)  
         make -j<number of CPU cores>
         sudo make install
      ```
  - Install Yosys  
3. **Install Bazel**
   ```
      sudo pacman -Syu bazel       # (Arch)
      sudo apt-get install bazel   # (Kali/Debian/Ubuntu)
   ```
4. **Build LiveHD**  
  LiveHD has several build options, detailed below.  All three should result in a working executable, but may differ in speed or output.  
  
  - Build LiveHD.  A binary will be created in `livehd/bazel-bin/main/lgshell`
      ```
          bazel build //main:all        # fast build
          bazel build //main:all -c opt # fast execution
          bazel build //main:all -c dbg # debug symbols
      ```
5. **Install pandoc (optional)**
   ```
      sudo pacman -Syu pandoc      # (Arch)  
      sudo apt-get install pandoc  # (Kali/Debian/Ubuntu)
   ```

## Potential issues

If you have multiple gcc versions, you may need to specify the latest. E.g:

```
    CXX=g++-8 CC=gcc-8 bazel build //main:all -c opt # fast execution for benchmarking
    CXX=g++-8 CC=gcc-8 bazel build //main:all -c dbg # debugging/development
```

If you want to run clang specific version:

```
    CXX=clang++-10 CC=clang-10 bazel build //main:all -c dbg # debugging/development
```

Make sure that the openJDK installed is compatible with bazel and has the certificates to use tools. E.g in debian:

```
    dpkg-reconfigure openjdk-11-jdk
    /var/lib/dpkg/ca-certificates-java.postinst configure
```

Make sure to have enough memory (4+GB at least)

## Next Steps

To start using LiveHD, check out [Usage](./Usage.md).  If you're interested in working on LiveHD, refer to [Develop](./Develop.md).

