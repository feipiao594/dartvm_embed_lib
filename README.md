# dartvm_embed_lib

Minimal embedder-facing library for Dart VM, wired to SDK GN artifacts.
The build now always produces two static variants:

- `libdartvm_embed_lib_jit.a`
- `libdartvm_embed_lib_aot.a`

## Build

```bash
cd dartvm_embed_lib
cmake -S . -B build -G Ninja
cmake --build build
```

Notes:
- Dart SDK runtime artifacts are always linked from static archives.
- `dartvm_embed_lib` is static-only.
- This project always builds both static libraries in one pass.

## API

Public header: `include/dartvm_embed_lib.h`

- `DartVmEmbed_Initialize`
- `DartVmEmbed_Cleanup`
- `DartVmEmbed_CreateIsolateFromKernel`
- `DartVmEmbed_CreateIsolateFromAppSnapshot`
- `DartVmEmbed_CreateIsolateFromProgramFile`
- `DartVmEmbed_RunEntry`
- `DartVmEmbed_RunLoop`
- `DartVmEmbed_ShutdownIsolate`

## Install As CMake Package

```bash
cd dartvm_embed_lib
cmake -S . -B build -G Ninja
cmake --build build
cmake --install build --prefix /tmp/dartvm_embed_install
```

Then external projects can use:

```cmake
find_package(dartvm_embed_lib REQUIRED CONFIG)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE DartVmEmbed::dartvm_embed_lib_jit) # or _aot
```

Configure external project with:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/tmp/dartvm_embed_install
```

## Helper For External CMake

Installed helper: `DartVmEmbedHelpers.cmake`

Use this to reduce external CMake boilerplate for Dart artifact generation:

```cmake
dartvm_embed_add_program_target(my_program_artifact
  DART_FILE "${CMAKE_CURRENT_SOURCE_DIR}/hello.dart"
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/program.bin"
  PUBSPEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}"   # optional
  EXTRA_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/other.dart"
  FLAVOR "jit" # or "aot"
)

add_executable(my_runner main.cpp)
target_link_libraries(my_runner PRIVATE DartVmEmbed::dartvm_embed_lib_jit)
add_dependencies(my_runner my_program_artifact)
```

`dartvm_embed_add_program_target` flavor behavior:
- `jit`: generates kernel `program.bin`
- `aot`: generates app-aot-elf `program.bin`
