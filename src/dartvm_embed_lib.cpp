#include "dartvm_embed_lib.h"

#include <assert.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

#if defined(DARTVM_EMBED_USE_BIN_NATIVE_RESOLVER)
#include <bin/builtin.h>
#include <bin/vmservice_impl.h>
#endif

#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
#include <bin/dartutils.h>
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
#include <bin/dfe.h>
#endif
#include <bin/isolate_data.h>
#include <bin/loader.h>
#include <include/dart_embedder_api.h>
#endif
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
#include <bin/elf_loader.h>
#endif
#include <include/dart_api.h>

static bool g_vm_initialized = false;
static std::unordered_map<Dart_Isolate, DartVmEmbedAotElfHandle>
    g_isolate_loaded_aot_elfs;
static std::unordered_map<Dart_Isolate, std::vector<uint8_t>>
    g_isolate_kernel_buffers;

#if defined(DARTVM_EMBED_USE_BIN_NATIVE_RESOLVER)
static bool SetupCoreNativeResolvers(char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  Dart_EnterScope();
  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kBuiltinLibrary);
  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kIOLibrary);
  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kCLILibrary);
  dart::bin::VmService::SetNativeResolver();
  Dart_ExitScope();
  return true;
}
#endif

static bool OnIsolateInitialize(void** child_callback_data, char** error) {
  if (child_callback_data != nullptr) {
    *child_callback_data = nullptr;
  }
  if (error != nullptr) {
    *error = nullptr;
  }
#if defined(DARTVM_EMBED_USE_BIN_NATIVE_RESOLVER)
  return SetupCoreNativeResolvers(error);
#else
  return true;
#endif
}

static void OnIsolateShutdown(void* isolate_group_data, void* isolate_data) {}

static void CleanupIsolate(void* isolate_group_data, void* callback_data) {}

static void CleanupGroup(void* callback_data) {}

#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
struct OwnedIsolateState {
  dart::bin::IsolateGroupData* isolate_group_data = nullptr;
  dart::bin::IsolateData* isolate_data = nullptr;
  bool owns_group = false;
  bool owns_isolate = false;
};

static std::unordered_map<Dart_Isolate, OwnedIsolateState> g_owned_isolates;
#endif

extern "C" {
#if defined(__GNUC__)
extern const uint8_t kDartVmSnapshotData[] __attribute__((weak));
extern const uint8_t kDartVmSnapshotInstructions[] __attribute__((weak));
#else
extern const uint8_t kDartVmSnapshotData[];
extern const uint8_t kDartVmSnapshotInstructions[];
#endif
}

static char* DupMessage(const char* message) {
  if (message == nullptr) {
    return nullptr;
  }
  const size_t len = strlen(message);
  char* out = reinterpret_cast<char*>(malloc(len + 1));
  if (out == nullptr) {
    return nullptr;
  }
  memcpy(out, message, len + 1);
  return out;
}

static bool IsUnsupportedVerifySdkHashFlag(const char* flag) {
  if (flag == nullptr) {
    return false;
  }
  return strcmp(flag, "verify_sdk_hash") == 0 ||
         strcmp(flag, "--verify_sdk_hash") == 0 ||
         strcmp(flag, "--no-verify_sdk_hash") == 0 ||
         strcmp(flag, "verify-sdk-hash") == 0 ||
         strcmp(flag, "--verify-sdk-hash") == 0 ||
         strcmp(flag, "--no-verify-sdk-hash") == 0;
}

static bool ReadProgramFile(const char* path,
                            std::vector<uint8_t>* out,
                            char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (path == nullptr || out == nullptr) {
    if (error != nullptr) {
      *error = DupMessage("ReadProgramFile: invalid argument.");
    }
    return false;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    if (error != nullptr) {
      *error = DupMessage("ReadProgramFile: failed to open program file.");
    }
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  if (size <= 0) {
    if (error != nullptr) {
      *error = DupMessage("ReadProgramFile: empty program file.");
    }
    return false;
  }
  out->resize(static_cast<size_t>(size));
  if (!f.read(reinterpret_cast<char*>(out->data()), size).good()) {
    if (error != nullptr) {
      *error = DupMessage("ReadProgramFile: failed to read program file.");
    }
    return false;
  }
  return true;
}

#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
static bool SetupCurrentIsolate(const char* script_uri,
                                dart::bin::IsolateData* isolate_data,
                                char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }

  Dart_EnterScope();
  Dart_Handle result =
      Dart_SetLibraryTagHandler(dart::bin::Loader::LibraryTagHandler);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }
  result = Dart_SetDeferredLoadHandler(dart::bin::Loader::DeferredLoadHandler);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }

  result = dart::bin::DartUtils::PrepareForScriptLoading(
      /*is_service_isolate=*/false, /*trace_loading=*/false);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }

  result = dart::bin::DartUtils::SetupPackageConfig(nullptr);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }

  result = Dart_SetEnvironmentCallback(dart::bin::DartUtils::EnvironmentCallback);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }

  result = dart::bin::Loader::InitForSnapshot(script_uri, isolate_data);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    Dart_ExitScope();
    return false;
  }

  Dart_ExitScope();
  return true;
}
#endif

extern "C" {

bool DartVmEmbed_Initialize(const DartVmEmbedInitConfig* config, char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }

  if (g_vm_initialized) {
    return true;
  }

#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  char* embedder_error = nullptr;
  if (!dart::embedder::InitOnce(&embedder_error)) {
    if (error != nullptr) {
      *error = DupMessage(embedder_error);
    }
    free(embedder_error);
    return false;
  }
  dart::bin::Loader::InitOnce();
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  dart::bin::dfe.Init();
  dart::bin::dfe.set_use_dfe();
  dart::bin::dfe.set_use_incremental_compiler(true);
#endif
#endif

  std::vector<const char*> vm_flags;
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  vm_flags.push_back("--precompilation");
#endif
  if (config != nullptr && config->vm_flag_count > 0 && config->vm_flags != nullptr) {
    for (int i = 0; i < config->vm_flag_count; ++i) {
      const char* flag = config->vm_flags[i];
      if (IsUnsupportedVerifySdkHashFlag(flag)) {
        continue;
      }
      vm_flags.push_back(flag);
    }
  }
  const char** vm_flags_ptr = vm_flags.empty() ? nullptr : vm_flags.data();
  char* vm_flag_error =
      Dart_SetVMFlags(static_cast<int>(vm_flags.size()), vm_flags_ptr);
  if (vm_flag_error != nullptr) {
    if (error != nullptr) {
      *error = DupMessage(vm_flag_error);
    }
    free(vm_flag_error);
    return false;
  }

  Dart_InitializeParams params;
  memset(&params, 0, sizeof(params));
  params.version = DART_INITIALIZE_PARAMS_CURRENT_VERSION;
  params.vm_snapshot_data =
      (config != nullptr && config->vm_snapshot_data_override != nullptr)
          ? config->vm_snapshot_data_override
          : kDartVmSnapshotData;
  params.vm_snapshot_instructions =
      (config != nullptr && config->vm_snapshot_instructions_override != nullptr)
          ? config->vm_snapshot_instructions_override
          : kDartVmSnapshotInstructions;
  params.start_kernel_isolate =
      (config == nullptr) ? true : config->start_kernel_isolate;
  params.initialize_isolate = OnIsolateInitialize;
  params.shutdown_isolate = OnIsolateShutdown;
  params.cleanup_isolate = CleanupIsolate;
  params.cleanup_group = CleanupGroup;
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  params.file_open = dart::bin::DartUtils::OpenFile;
  params.file_read = dart::bin::DartUtils::ReadFile;
  params.file_write = dart::bin::DartUtils::WriteFile;
  params.file_close = dart::bin::DartUtils::CloseFile;
  params.entropy_source = dart::bin::DartUtils::EntropySource;
#endif

  char* init_error = Dart_Initialize(&params);
  if (init_error != nullptr) {
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
    dart::embedder::Cleanup();
#endif
    if (error != nullptr) {
      *error = DupMessage(init_error);
    }
    free(init_error);
    return false;
  }

  g_vm_initialized = true;
  return true;
}

bool DartVmEmbed_Cleanup(char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }

  if (!g_vm_initialized) {
    return true;
  }

  char* cleanup_error = Dart_Cleanup();
  if (cleanup_error != nullptr) {
    if (error != nullptr) {
      *error = DupMessage(cleanup_error);
    }
    free(cleanup_error);
    return false;
  }

  g_vm_initialized = false;
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  dart::embedder::Cleanup();
#endif
  return true;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromKernel(const char* script_uri,
                                                 const char* name,
                                                 const uint8_t* kernel_buffer,
                                                 intptr_t kernel_buffer_size,
                                                 void* isolate_group_data,
                                                 void* isolate_data,
                                                 char** error) {
  auto ensure_error = [&](const char* message) {
    if (error != nullptr && *error == nullptr) {
      *error = DupMessage(message);
    }
  };
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    group_data = new dart::bin::IsolateGroupData(script_uri, nullptr, nullptr,
                                                 /*isolate_run_app_snapshot=*/false);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  auto* local_isolate_data = reinterpret_cast<dart::bin::IsolateData*>(isolate_data);
  if (local_isolate_data == nullptr) {
    local_isolate_data = new dart::bin::IsolateData(group_data);
    owned.isolate_data = local_isolate_data;
    owned.owns_isolate = true;
  }
#endif

  Dart_IsolateFlags flags;
  Dart_IsolateFlagsInitialize(&flags);
  flags.null_safety = true;
  flags.snapshot_is_dontneed_safe = false;
  flags.load_vmservice_library = false;
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  void* actual_group_data =
      isolate_group_data ? isolate_group_data : owned.isolate_group_data;
  void* actual_isolate_data =
      isolate_data ? isolate_data : owned.isolate_data;
#else
  void* actual_group_data = isolate_group_data;
  void* actual_isolate_data = isolate_data;
#endif
  const uint8_t* platform_kernel_buffer = nullptr;
  intptr_t platform_kernel_buffer_size = 0;
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  dart::bin::dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
#endif
#endif
  if (platform_kernel_buffer == nullptr || platform_kernel_buffer_size == 0) {
    platform_kernel_buffer = kernel_buffer;
    platform_kernel_buffer_size = kernel_buffer_size;
  }

  Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
      script_uri, name, platform_kernel_buffer, platform_kernel_buffer_size,
      &flags, actual_group_data, actual_isolate_data, error);
  if (isolate == nullptr && error != nullptr && *error == nullptr) {
    *error = DupMessage("Dart_CreateIsolateGroupFromKernel returned null without an error message.");
  }
  bool resolver_ok = true;
#if defined(DARTVM_EMBED_USE_BIN_NATIVE_RESOLVER)
  if (isolate != nullptr) {
    resolver_ok = SetupCoreNativeResolvers(error);
  }
#endif
  if (isolate != nullptr && !resolver_ok) {
    ensure_error("SetupCoreNativeResolvers failed.");
    Dart_ShutdownIsolate();
    return nullptr;
  }

#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    ensure_error("Dart_CreateIsolateGroupFromKernel failed.");
    fprintf(stderr, "[embed] fail@create error=%s\n",
            (error && *error) ? *error : "(null)");
    return nullptr;
  }

  if (!SetupCurrentIsolate(script_uri, local_isolate_data, error)) {
    Dart_ShutdownIsolate();
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    ensure_error("SetupCurrentIsolate failed.");
    fprintf(stderr, "[embed] fail@setup error=%s\n",
            (error && *error) ? *error : "(null)");
    return nullptr;
  }

  Dart_EnterScope();
  Dart_Handle load_result =
      Dart_LoadScriptFromKernel(kernel_buffer, kernel_buffer_size);
  if (Dart_IsError(load_result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(load_result));
    }
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    ensure_error("Dart_LoadScriptFromKernel failed.");
    fprintf(stderr, "[embed] fail@load error=%s\n",
            (error && *error) ? *error : "(null)");
    return nullptr;
  }
  Dart_ExitScope();

  Dart_ExitIsolate();
  char* make_runnable_error = Dart_IsolateMakeRunnable(isolate);
  if (make_runnable_error != nullptr) {
    if (error != nullptr) {
      *error = make_runnable_error;
    } else {
      free(make_runnable_error);
    }
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    ensure_error("Dart_IsolateMakeRunnable failed.");
    fprintf(stderr, "[embed] fail@runnable error=%s\n",
            (error && *error) ? *error : "(null)");
    return nullptr;
  }

  if (owned.owns_isolate || owned.owns_group) {
    g_owned_isolates[isolate] = owned;
  }
#endif

  return isolate;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromAppSnapshot(
    const char* script_uri,
    const char* name,
    const uint8_t* isolate_snapshot_data,
    const uint8_t* isolate_snapshot_instructions,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    group_data = new dart::bin::IsolateGroupData(script_uri, nullptr, nullptr,
                                                 /*isolate_run_app_snapshot=*/true);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  auto* local_isolate_data = reinterpret_cast<dart::bin::IsolateData*>(isolate_data);
  if (local_isolate_data == nullptr) {
    local_isolate_data = new dart::bin::IsolateData(group_data);
    owned.isolate_data = local_isolate_data;
    owned.owns_isolate = true;
  }
#endif

  Dart_IsolateFlags flags;
  Dart_IsolateFlagsInitialize(&flags);
  flags.null_safety = true;
  flags.snapshot_is_dontneed_safe = false;
  flags.load_vmservice_library = false;
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  void* actual_group_data =
      isolate_group_data ? isolate_group_data : owned.isolate_group_data;
  void* actual_isolate_data =
      isolate_data ? isolate_data : owned.isolate_data;
#else
  void* actual_group_data = isolate_group_data;
  void* actual_isolate_data = isolate_data;
#endif
  Dart_Isolate isolate =
      Dart_CreateIsolateGroup(script_uri, name, isolate_snapshot_data,
                              isolate_snapshot_instructions, &flags,
                              actual_group_data, actual_isolate_data, error);
  if (isolate == nullptr && error != nullptr && *error == nullptr) {
    *error = DupMessage("Dart_CreateIsolateGroup returned null without an error message.");
  }
  bool resolver_ok = true;
#if defined(DARTVM_EMBED_USE_BIN_NATIVE_RESOLVER)
  if (isolate != nullptr) {
    resolver_ok = SetupCoreNativeResolvers(error);
  }
#endif
  if (isolate != nullptr && !resolver_ok) {
    Dart_ShutdownIsolate();
    return nullptr;
  }
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  if (!SetupCurrentIsolate(script_uri, local_isolate_data, error)) {
    Dart_ShutdownIsolate();
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }
  Dart_ExitIsolate();
  char* make_runnable_error = Dart_IsolateMakeRunnable(isolate);
  if (make_runnable_error != nullptr) {
    if (error != nullptr) {
      *error = make_runnable_error;
    } else {
      free(make_runnable_error);
    }
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }
  if (owned.owns_isolate || owned.owns_group) {
    g_owned_isolates[isolate] = owned;
  }
#endif

  return isolate;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromProgramFile(
    const char* program_path,
    const char* script_uri,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (program_path == nullptr) {
    if (error != nullptr) {
      *error = DupMessage(
          "DartVmEmbed_CreateIsolateFromProgramFile: program_path is null.");
    }
    return nullptr;
  }

  const char* actual_script_uri =
      (script_uri != nullptr) ? script_uri : program_path;
  const char* isolate_name = "isolate";

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  DartVmEmbedAotElfHandle loaded_elf = nullptr;
  const uint8_t *vm_data = nullptr, *vm_instr = nullptr;
  const uint8_t *iso_data = nullptr, *iso_instr = nullptr;
  if (!DartVmEmbed_LoadAotElf(program_path, 0, &loaded_elf, &vm_data, &vm_instr,
                              &iso_data, &iso_instr, error)) {
    return nullptr;
  }

  DartVmEmbedInitConfig config;
  config.start_kernel_isolate = false;
  config.vm_snapshot_data_override = vm_data;
  config.vm_snapshot_instructions_override = vm_instr;
  if (!DartVmEmbed_Initialize(&config, error)) {
    DartVmEmbed_UnloadAotElf(loaded_elf);
    return nullptr;
  }

  Dart_Isolate isolate = DartVmEmbed_CreateIsolateFromAppSnapshot(
      actual_script_uri, isolate_name, iso_data, iso_instr, isolate_group_data,
      isolate_data, error);
  if (isolate == nullptr) {
    DartVmEmbed_UnloadAotElf(loaded_elf);
    return nullptr;
  }
  g_isolate_loaded_aot_elfs[isolate] = loaded_elf;
  return isolate;
#else
  const char* vm_flags[] = {"--no-precompilation"};
  DartVmEmbedInitConfig config;
  config.start_kernel_isolate = false;
  config.vm_flag_count = 1;
  config.vm_flags = vm_flags;
  if (!DartVmEmbed_Initialize(&config, error)) {
    return nullptr;
  }

  std::vector<uint8_t> kernel;
  if (!ReadProgramFile(program_path, &kernel, error)) {
    return nullptr;
  }

  Dart_Isolate isolate = DartVmEmbed_CreateIsolateFromKernel(
      actual_script_uri, isolate_name, kernel.data(),
      static_cast<intptr_t>(kernel.size()), isolate_group_data, isolate_data,
      error);
  if (isolate != nullptr) {
    g_isolate_kernel_buffers.emplace(isolate, std::move(kernel));
  }
  return isolate;
#endif
}

bool DartVmEmbed_LoadAotElf(
    const char* path,
    int64_t file_offset,
    DartVmEmbedAotElfHandle* out_handle,
    const uint8_t** out_vm_snapshot_data,
    const uint8_t** out_vm_snapshot_instructions,
    const uint8_t** out_isolate_snapshot_data,
    const uint8_t** out_isolate_snapshot_instructions,
    char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (out_handle == nullptr || out_vm_snapshot_data == nullptr ||
      out_vm_snapshot_instructions == nullptr ||
      out_isolate_snapshot_data == nullptr ||
      out_isolate_snapshot_instructions == nullptr) {
    if (error != nullptr) {
      *error = DupMessage("DartVmEmbed_LoadAotElf: output pointers must not be null.");
    }
    return false;
  }

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  const char* load_error = nullptr;
  Dart_LoadedElf* loaded =
      Dart_LoadELF(path, file_offset, &load_error, out_vm_snapshot_data,
                   out_vm_snapshot_instructions, out_isolate_snapshot_data,
                   out_isolate_snapshot_instructions);
  if (loaded == nullptr) {
    if (error != nullptr) {
      *error = DupMessage(load_error != nullptr ? load_error
                                                : "Dart_LoadELF failed.");
    }
    return false;
  }
  *out_handle = reinterpret_cast<DartVmEmbedAotElfHandle>(loaded);
  return true;
#else
  (void)path;
  (void)file_offset;
  (void)out_handle;
  (void)out_vm_snapshot_data;
  (void)out_vm_snapshot_instructions;
  (void)out_isolate_snapshot_data;
  (void)out_isolate_snapshot_instructions;
  if (error != nullptr) {
    *error = DupMessage(
        "DartVmEmbed_LoadAotElf is only available in AOT runtime flavor.");
  }
  return false;
#endif
}

void DartVmEmbed_UnloadAotElf(DartVmEmbedAotElfHandle handle) {
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (handle == nullptr) {
    return;
  }
  Dart_UnloadELF(reinterpret_cast<Dart_LoadedElf*>(handle));
#else
  (void)handle;
#endif
}

Dart_Handle DartVmEmbed_RunEntry(Dart_Handle library, const char* entry_name) {
  const intptr_t kNumIsolateArgs = 2;

  const char* actual_entry = (entry_name != nullptr) ? entry_name : "main";
  Dart_Handle entry = Dart_NewStringFromCString(actual_entry);
  Dart_Handle entry_closure = Dart_GetField(library, entry);
  if (Dart_IsError(entry_closure)) {
    // Some build modes may not expose a top-level getter for the function.
    Dart_Handle invoke_result = Dart_Invoke(library, entry, 0, nullptr);
    if (Dart_IsError(invoke_result)) {
      return invoke_result;
    }
    return Dart_RunLoop();
  }
  if (!Dart_IsClosure(entry_closure)) {
    return entry_closure;
  }

  Dart_Handle isolate_lib_name = Dart_NewStringFromCString("dart:isolate");
  Dart_Handle isolate_lib = Dart_LookupLibrary(isolate_lib_name);
  if (Dart_IsError(isolate_lib)) {
    return isolate_lib;
  }

  Dart_Handle start_name = Dart_NewStringFromCString("_startMainIsolate");
  Dart_Handle isolate_args[kNumIsolateArgs] = {entry_closure, Dart_Null()};
  Dart_Handle result =
      Dart_Invoke(isolate_lib, start_name, kNumIsolateArgs, isolate_args);
  if (Dart_IsError(result)) {
    return result;
  }

  return Dart_RunLoop();
}

Dart_Handle DartVmEmbed_RunRootEntry(const char* entry_name) {
  Dart_Handle library = Dart_RootLibrary();
  if (Dart_IsError(library)) {
    return library;
  }
  return DartVmEmbed_RunEntry(library, entry_name);
}

bool DartVmEmbed_RunRootEntryChecked(const char* entry_name, char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  Dart_Handle result = DartVmEmbed_RunRootEntry(entry_name);
  if (Dart_IsError(result)) {
    if (error != nullptr) {
      *error = DupMessage(Dart_GetError(result));
    }
    return false;
  }
  return true;
}

bool DartVmEmbed_RunRootEntryOnIsolate(Dart_Isolate isolate,
                                       const char* entry_name,
                                       char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (isolate == nullptr) {
    if (error != nullptr) {
      *error = DupMessage("DartVmEmbed_RunRootEntryOnIsolate: isolate is null.");
    }
    return false;
  }
  bool entered_isolate = false;
  if (Dart_CurrentIsolate() == nullptr) {
    Dart_EnterIsolate(isolate);
    entered_isolate = true;
  }

  Dart_EnterScope();
  const bool ok = DartVmEmbed_RunRootEntryChecked(entry_name, error);
  Dart_ExitScope();

  if (entered_isolate) {
    Dart_ExitIsolate();
  }
  return ok;
}

Dart_Handle DartVmEmbed_RunLoop(void) {
  return Dart_RunLoop();
}

void DartVmEmbed_ShutdownIsolate(void) {
  Dart_Isolate isolate = Dart_CurrentIsolate();
  if (isolate != nullptr) {
    Dart_ShutdownIsolate();
  }
  if (isolate != nullptr) {
    auto it = g_isolate_loaded_aot_elfs.find(isolate);
    if (it != g_isolate_loaded_aot_elfs.end()) {
      DartVmEmbed_UnloadAotElf(it->second);
      g_isolate_loaded_aot_elfs.erase(it);
    }
    g_isolate_kernel_buffers.erase(isolate);
  }
#if defined(DARTVM_EMBED_ENABLE_FULL_ISOLATE_SETUP)
  if (isolate != nullptr) {
    auto it = g_owned_isolates.find(isolate);
    if (it != g_owned_isolates.end()) {
      if (it->second.owns_isolate) {
        delete it->second.isolate_data;
      }
      if (it->second.owns_group) {
        delete it->second.isolate_group_data;
      }
      g_owned_isolates.erase(it);
    }
  }
#endif
}

void DartVmEmbed_ShutdownIsolateByHandle(Dart_Isolate isolate) {
  if (isolate == nullptr) {
    return;
  }
  if (Dart_CurrentIsolate() == nullptr) {
    Dart_EnterIsolate(isolate);
  }
  DartVmEmbed_ShutdownIsolate();
}

}  // extern "C"
