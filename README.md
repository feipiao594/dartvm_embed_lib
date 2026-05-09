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

Install a local external copy:

```bash
cd dartvm_embed_lib
cmake --install build --prefix lib_install
```

## API

Public header: `include/dartvm_embed_lib.h`

- `DartVmEmbed_Initialize`
- `DartVmEmbed_Cleanup`
- `DartVmEmbed_CreateIsolateFromKernel`
- `DartVmEmbed_CreateIsolateFromAppSnapshot`
- `DartVmEmbed_RunEntry`
- `DartVmEmbed_RunLoop`
- `DartVmEmbed_ShutdownIsolate`

## App Starter

`example/CMakeLists.txt` owns the app-facing configuration:

- `APP_RUNTIME_FLAVOR=jit|jit_source|aot`

The internal helper under `example/internal/cmake/` only provides build-system
configuration functions. It does not define the app target for you.

By default, the starter consumes the installed library from `lib_install/`.

```bash
cd dartvm_embed_lib
cmake --install build --prefix lib_install
cmake -S example -B example/build -G Ninja \
  -DAPP_RUNTIME_FLAVOR=jit \
cmake --build example/build
```

Run the app:

```bash
cd example/build
./app
```

Hot reload workflow:

```bash
cmake --install build --prefix lib_install
cmake -S example -B example/build -G Ninja \
  -DAPP_RUNTIME_FLAVOR=jit_source
cmake --build example/build
cd example/build
./app
cd ../..
./reload_sources.sh
```

Switch to AOT:

```bash
cmake --install build --prefix lib_install
cmake -S example -B example/build -G Ninja \
  -DAPP_RUNTIME_FLAVOR=aot
cmake --build example/build
cd example/build
./app
```

## Internal Design Doc (ZH)

- `docs/IMPLEMENTATION_ZH.md`: implementation details, packaging rationale, known pitfalls, and VM Service roadmap.
