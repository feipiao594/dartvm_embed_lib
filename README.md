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

## Example

See https://github.com/feipiao594/dartvm_embeding_example
