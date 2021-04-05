# Installation

This is a high level description of how to build LiveHD.

## Requirements

Although LiveHD should run on most common Linux distributions, it is heavily tested on both Arch and Kali (Debian based).

The following programs are assumed to be present when building LiveHD:
 - GCC 8+ or Clang 8+ (C++17 support is required)
 - Bazel
 - python3

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


## Steps

1. **Download LiveHD source**  
      ```
        git clone https://github.com/masc-ucsc/livehd
      ```
2. **Install Bazel**
  - For Debian-derived distros (including Ubuntu and Kali), follow [these](https://docs.bazel.build/versions/master/install-ubuntu.html) instructions.
  - For Arch-derived distros, install the `bazel` package with `sudo pacman -Syu bazel`.
4. **Build LiveHD**  
  LiveHD has several build options, detailed below.  All three should result in a working executable, but may differ in speed or output.  A binary will be created in `livehd/bazel-bin/main/lgshell`.
      ```
          bazel build //main:all        # fast build, no debug symbols, slow execution (default)
          bazel build //main:all -c opt # fastest execution speed, no debug symbols, no assertions
          bazel build //main:all -c dbg # moderate execution speed, debug symbols
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

If you fail to build for the first time, you may need to clear the cache under your home directory before rebuilding:

```
    rm -rf ~/.cache/bazel
```

Make sure to have enough memory (4+GB at least)

## Next Steps

To start using LiveHD, check out [Usage](./Usage.md).  If you're interested in working on LiveHD, refer to [Develop](./Develop.md).

