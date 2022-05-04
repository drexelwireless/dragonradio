# Developing with the Radio

The radio image contains a pre-built version of DragonRadio in `/root/dragonradio`. Although it is possible to develop the radio as the ``root`` user, mounting the host machine user's home directory in the container as described in {ref}`sec:container` is recommended. The following instructions assume you are in a directory containing the DragonRadio source.

A full DragonRadio installation, including all dependencies, can be performed by running the `build.sh` script. If you are using a DragonRadio LXC container built as described in {ref}`sec:building_container`, then you do not need to perform a full build with `build.sh` since the container already has all dependencies built and installed. Instead, you can build only DragonRadio itself.

DragonRadio is built using `cmake`, and the standard `cmake` variables and configuration options apply. From the directory containing the DragonRadio source, the standard  `cmake` build sequence suffices to build the radio:

```shell-session
$ mkdir build
$ cd build
$ cmake ..
$ make -j10
```

If Ninja is installed (`sudo apt install ninja-build`), it can be used to build DragonRadio instead:

```shell-session
$ mkdir build
$ cd build
$ cmake .. -GNinja
$ ninja
```

Incremental builds can then be performed using `make` (or `ninja`) in the `build` directory.

## Using a Different Compiler

By default, the currently installed version of GCC is used. On Ubuntu 20.04, this is GCC 9. The C and C++ compiler used can be changed by setting the `CC` and `CXX` variables when invoking `cmake`. For example, the radio can be built with LLVM 14 as follows:

```shell-session
$ CC=clang-14 CXX=clang++-14 cmake ..
```

## Debug Build

By default, `cmake` will build the release configuration. To build the debug configuration, set the `CMAKE_BUILD_TYPE` `cmake` variable.

```shell-session
$ cmake -DCMAKE_BUILD_TYPE=Debug ..
```

The debug configurations sets the `-O1` optimization level and includes debug symbols.

It is also possible to enable the various GCC/clang sanitizers by modifying the top-level `CMakeLists.txt` file and adding additional options after the section where `CMAKE_CXX_*` variables are set. For example, to enable the address sanitizer, add the following options:


```cmake
add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
add_link_options(-fsanitize=address -fno-omit-frame-pointer)
```
