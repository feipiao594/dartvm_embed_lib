#pragma once

#include <stdint.h>

typedef struct _Dart_Isolate* Dart_Isolate;
typedef struct _Dart_Handle* Dart_Handle;

#if defined(_WIN32)
  #if defined(DARTVM_EMBED_LIB_EXPORTING)
    #define DARTVM_EMBED_LIB_EXPORT __declspec(dllexport)
  #else
    #define DARTVM_EMBED_LIB_EXPORT __declspec(dllimport)
  #endif
#else
  #define DARTVM_EMBED_LIB_EXPORT
#endif

extern "C" {

struct DartVmEmbedInitConfig {
  bool start_kernel_isolate;
  const uint8_t* vm_snapshot_data_override;
  const uint8_t* vm_snapshot_instructions_override;
  int vm_flag_count;
  const char** vm_flags;

  DartVmEmbedInitConfig()
      : start_kernel_isolate(true),
        vm_snapshot_data_override(nullptr),
        vm_snapshot_instructions_override(nullptr),
        vm_flag_count(0),
        vm_flags(nullptr) {}
};

// Opaque handle returned by AOT ELF loader.
typedef void* DartVmEmbedAotElfHandle;

// Initializes embedder + Dart VM.
// Returns true on success. On error, *error receives malloc-allocated message.
DARTVM_EMBED_LIB_EXPORT bool DartVmEmbed_Initialize(
    const DartVmEmbedInitConfig* config,
    char** error);

// Cleans up Dart VM + embedder.
// Returns true on success. On error, *error receives malloc-allocated message.
DARTVM_EMBED_LIB_EXPORT bool DartVmEmbed_Cleanup(char** error);

// Creates a root isolate group from a kernel (.dill) buffer.
DARTVM_EMBED_LIB_EXPORT Dart_Isolate DartVmEmbed_CreateIsolateFromKernel(
    const char* script_uri,
    const char* name,
    const uint8_t* kernel_buffer,
    intptr_t kernel_buffer_size,
    void* isolate_group_data,
    void* isolate_data,
    char** error);

// Creates a root isolate group from app snapshot pieces (AOT/AppJIT style).
DARTVM_EMBED_LIB_EXPORT Dart_Isolate DartVmEmbed_CreateIsolateFromAppSnapshot(
    const char* script_uri,
    const char* name,
    const uint8_t* isolate_snapshot_data,
    const uint8_t* isolate_snapshot_instructions,
    void* isolate_group_data,
    void* isolate_data,
    char** error);

// Creates a root isolate from a program file.
// - jit runtime: expects a kernel file (for example .dill)
// - aot runtime: expects an app-aot-elf file (for example .aot)
// This function also initializes VM when needed.
DARTVM_EMBED_LIB_EXPORT Dart_Isolate DartVmEmbed_CreateIsolateFromProgramFile(
    const char* program_path,
    const char* script_uri,
    void* isolate_group_data,
    void* isolate_data,
    char** error);

// Loads an app-aot-elf snapshot and returns VM/Isolate snapshot pointers.
// Returns true on success. On error, *error receives malloc-allocated message.
DARTVM_EMBED_LIB_EXPORT bool DartVmEmbed_LoadAotElf(
    const char* path,
    int64_t file_offset,
    DartVmEmbedAotElfHandle* out_handle,
    const uint8_t** out_vm_snapshot_data,
    const uint8_t** out_vm_snapshot_instructions,
    const uint8_t** out_isolate_snapshot_data,
    const uint8_t** out_isolate_snapshot_instructions,
    char** error);

// Unloads an ELF loaded by DartVmEmbed_LoadAotElf.
DARTVM_EMBED_LIB_EXPORT void DartVmEmbed_UnloadAotElf(
    DartVmEmbedAotElfHandle handle);

// Calls _startMainIsolate(entry, null) and then Dart_RunLoop.
// If entry_name is null, "main" is used.
DARTVM_EMBED_LIB_EXPORT Dart_Handle DartVmEmbed_RunEntry(
    Dart_Handle library,
    const char* entry_name);

// Runs entry on Dart_RootLibrary() and then Dart_RunLoop.
// If entry_name is null, "main" is used.
DARTVM_EMBED_LIB_EXPORT Dart_Handle DartVmEmbed_RunRootEntry(
    const char* entry_name);

// Same as DartVmEmbed_RunRootEntry but returns error text instead of Dart_Handle.
DARTVM_EMBED_LIB_EXPORT bool DartVmEmbed_RunRootEntryChecked(
    const char* entry_name,
    char** error);

// Runs root entry on the provided isolate.
// This helper enters isolate/scope internally and exits them before return.
DARTVM_EMBED_LIB_EXPORT bool DartVmEmbed_RunRootEntryOnIsolate(
    Dart_Isolate isolate,
    const char* entry_name,
    char** error);

// Runs the isolate message loop until completion.
DARTVM_EMBED_LIB_EXPORT Dart_Handle DartVmEmbed_RunLoop(void);

// Shuts down current isolate.
DARTVM_EMBED_LIB_EXPORT void DartVmEmbed_ShutdownIsolate(void);

// Shuts down isolate by handle (enters isolate internally when needed).
DARTVM_EMBED_LIB_EXPORT void DartVmEmbed_ShutdownIsolateByHandle(
    Dart_Isolate isolate);
}
