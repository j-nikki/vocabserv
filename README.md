# vocabserv

Serves files from `src/res` to user. Additionally implements an API accessible from `api/...`.

Example vocabulary file:
```
my term 1
my definition for term 1
my term 2
my definition for term 2
```

## Requirements
* [vcpkg](https://vcpkg.io/en/index.html)
* Modern C++ build toolds; tested on VS22 and GCC11

## Building and running
1. `vcpkg install boost-asio fmt spdlog magic-enum`
1. `cmake -B <build directory> -S . -DCMAKE_TOOLCHAIN_FILE=<path to vcpkg>/scripts/buildsystems/vcpkg.cmake`
1. `cmake --build <build directory>`
1. `build/src/vocabserv <vocab path> [<port to run on>] [<log file prefix>]`
