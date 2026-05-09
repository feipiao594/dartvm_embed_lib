#include "dartvm_embed_lib.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <bin/isolate_data.h>
#include <bin/builtin.h>
#include <bin/dartdev_isolate.h>
#include <bin/dfe.h>
#include <bin/dartutils.h>
#include <bin/file.h>
#include <bin/loader.h>
#include <bin/main_options.h>
#include <bin/process.h>
#include <bin/snapshot_utils.h>
#include <bin/utils.h>
#include <platform/syslog.h>
#include <bin/vmservice_impl.h>
#include <include/bin/dart_io_api.h>
#include <include/bin/native_assets_api.h>
#include <include/dart_api.h>
#include <include/dart_embedder_api.h>
#include <include/dart_tools_api.h>
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
#include <bin/elf_loader.h>
#endif

extern "C" {
#if defined(__GNUC__)
extern const uint8_t kDartVmSnapshotData[] __attribute__((weak));
extern const uint8_t kDartVmSnapshotInstructions[] __attribute__((weak));
extern const uint8_t kDartCoreIsolateSnapshotData[] __attribute__((weak));
extern const uint8_t kDartCoreIsolateSnapshotInstructions[] __attribute__((weak));
#else
extern const uint8_t kDartVmSnapshotData[];
extern const uint8_t kDartVmSnapshotInstructions[];
extern const uint8_t kDartCoreIsolateSnapshotData[];
extern const uint8_t kDartCoreIsolateSnapshotInstructions[];
#endif
}

// Tools
class DartVmEmbedState {
public:
  static DartVmEmbedState &Instance() {
    static DartVmEmbedState instance;
    return instance;
  }

  DartVmEmbedState(const DartVmEmbedState &) = delete;
  DartVmEmbedState &operator=(const DartVmEmbedState &) = delete;

  std::unordered_set<dart::bin::IsolateData *> callback_owned_isolate_data;
  std::unordered_set<dart::bin::IsolateGroupData *> callback_owned_isolate_group_data;

  bool vm_initialized = false;
  const uint8_t* vm_snapshot_data = nullptr;
  const uint8_t* vm_snapshot_instructions = nullptr;
  DartVmEmbedFileModifiedCallback file_modified_callback = nullptr;

  std::unordered_map<Dart_Isolate, DartVmEmbedAotElfHandle>
      isolate_loaded_aot_elfs;
  std::unordered_map<Dart_Isolate, std::vector<uint8_t>> isolate_kernel_buffers;

  // Mirrors runtime/bin/main_impl.cc global snapshot state for best
  // compatibility with isolate spawning (spawnUri) and service isolate setup.
  //
  // DIFF(main_impl): main_impl uses file-scope globals; this library stores
  // them in a singleton to avoid exporting mutable globals.
  const uint8_t* app_isolate_snapshot_data = nullptr;
  const uint8_t* app_isolate_snapshot_instructions = nullptr;
  std::string app_script_uri;

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  std::unique_ptr<dart::bin::AppSnapshot> app_snapshot;  // JIT app snapshot.
#endif
  DartVmEmbedAotElfHandle app_aot_elf_handle = nullptr;
  int app_aot_elf_refcount = 0;

  struct OwnedIsolateState {
    dart::bin::IsolateGroupData* isolate_group_data = nullptr;
    dart::bin::IsolateData* isolate_data = nullptr;
    bool owns_group = false;
    bool owns_isolate = false;
  };
  std::unordered_map<Dart_Isolate, OwnedIsolateState> owned_isolates;

  std::string vm_service_ip = "127.0.0.1";
  int vm_service_port = 8181;
  bool vm_service_auth_codes_disabled = true;

private:
  DartVmEmbedState() = default;
};

static DartVmEmbedState &State() { return DartVmEmbedState::Instance(); }

static char *DupMessage(const char *message) {
  if (message == nullptr) {
    return nullptr;
  }
  const size_t len = strlen(message);
  char *out = reinterpret_cast<char *>(malloc(len + 1));
  if (out == nullptr) {
    return nullptr;
  }
  memcpy(out, message, len + 1);
  return out;
}

static void SetErrorIfUnset(char** error, const char* message) {
  if (error != nullptr && *error == nullptr) {
    *error = DupMessage(message);
  }
}

static bool SetErrorFromHandle(Dart_Handle result, char** error) {
  if (!Dart_IsError(result)) {
    return false;
  }
  if (error != nullptr) {
    *error = DupMessage(Dart_GetError(result));
  }
  return true;
}

static const char* SanitizePathLikeMain(const char* path, std::string* storage) {
  if (path == nullptr) {
    return nullptr;
  }
#if defined(DART_HOST_OS_WINDOWS)
  const size_t len = strlen(path);
  storage->clear();
  storage->reserve(len + 2);
  if (len > 2 && path[1] == ':') {
    storage->push_back('/');
  }
  for (const char* p = path; *p != '\0'; ++p) {
    storage->push_back(*p == '\\' ? '/' : *p);
  }
  return storage->c_str();
#else
  *storage = path;
  return storage->c_str();
#endif
}

static const char *EffectivePackagesConfig(const char *packages_config) {
  if (packages_config != nullptr) {
    return packages_config;
  }
  return dart::bin::Options::packages_file();
}

// other func
static bool ReadProgramFile(const char* path,
                            std::vector<uint8_t>* out,
                            char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (path == nullptr || out == nullptr) {
    SetErrorIfUnset(error, "ReadProgramFile: invalid argument.");
    return false;
  }
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    SetErrorIfUnset(error, "ReadProgramFile: failed to open program file.");
    return false;
  }
  f.seekg(0, std::ios::end);
  const std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  if (size <= 0) {
    SetErrorIfUnset(error, "ReadProgramFile: empty program file.");
    return false;
  }
  out->resize(static_cast<size_t>(size));
  if (!f.read(reinterpret_cast<char*>(out->data()), size).good()) {
    SetErrorIfUnset(error, "ReadProgramFile: failed to read program file.");
    return false;
  }
  return true;
}

static bool ShouldEnableVmService(void) {
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // VM service and hot reload are not supported in AOT precompiled runtime.
  return false;
#else
  const char* hot_reload = getenv("DARTVM_EMBED_HOT_RELOAD");
  return hot_reload != nullptr && strcmp(hot_reload, "1") == 0;
#endif
}

static bool ComputeSnapshotIsDontneedSafe(void) {
  bool dontneed_safe = true;
#if defined(DART_HOST_OS_LINUX)
  // Mirrors main_impl: some Linux environments cannot safely madvise(DONT_NEED)
  // on snapshot mappings.
  dontneed_safe = false;
#elif defined(DEBUG)
  if (dart::bin::Options::force_load_elf_from_memory()) {
    dontneed_safe = false;
  }
#endif
  return dontneed_safe;
}

static bool FileModifiedCallback(const char* url, int64_t since) {
  auto path = dart::bin::File::UriToPath(url);
  if (path == nullptr) {
    return true;
  }
  int64_t data[dart::bin::File::kStatSize];
  dart::bin::File::Stat(nullptr, path.get(), data);
  if (data[dart::bin::File::kType] == dart::bin::File::kDoesNotExist) {
    return true;
  }
  return data[dart::bin::File::kModifiedTime] > since;
}

static constexpr const char* kStdoutStreamId = "Stdout";
static constexpr const char* kStderrStreamId = "Stderr";

static bool ServiceStreamListenCallback(const char* stream_id) {
  if (strcmp(stream_id, kStdoutStreamId) == 0) {
    dart::bin::SetCaptureStdout(true);
    return true;
  } else if (strcmp(stream_id, kStderrStreamId) == 0) {
    dart::bin::SetCaptureStderr(true);
    return true;
  }
  return false;
}

static void ServiceStreamCancelCallback(const char* stream_id) {
  if (strcmp(stream_id, kStdoutStreamId) == 0) {
    dart::bin::SetCaptureStdout(false);
  } else if (strcmp(stream_id, kStderrStreamId) == 0) {
    dart::bin::SetCaptureStderr(false);
  }
}

static Dart_Handle SetupCoreLibraries(Dart_Isolate isolate,
                                      dart::bin::IsolateData* isolate_data,
                                      bool is_isolate_group_start,
                                      bool is_kernel_isolate,
                                      const char** resolved_packages_config) {
  auto* isolate_group_data = isolate_data->isolate_group_data();
  const char* packages_file = isolate_data->packages_file();
  const char* script_uri = isolate_group_data->script_url;

  Dart_Handle result =
      dart::bin::DartUtils::PrepareForScriptLoading(
          /*is_service_isolate=*/false,
          dart::bin::Options::trace_loading());
  if (Dart_IsError(result)) {
    return result;
  }

  result = dart::bin::DartUtils::SetupPackageConfig(packages_file);
  if (Dart_IsError(result)) {
    return result;
  }

  if (!Dart_IsNull(result) && resolved_packages_config != nullptr) {
    result = Dart_StringToCString(result, resolved_packages_config);
    if (Dart_IsError(result)) {
      return result;
    }
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
    if (is_isolate_group_start) {
      isolate_group_data->set_resolved_packages_config(*resolved_packages_config);
    } else {
      ASSERT(isolate_group_data->resolved_packages_config() == nullptr ||
             strcmp(isolate_group_data->resolved_packages_config(),
                    *resolved_packages_config) == 0);
    }
#endif
  }

  result = Dart_SetEnvironmentCallback(dart::bin::DartUtils::EnvironmentCallback);
  if (Dart_IsError(result)) {
    return result;
  }

  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kBuiltinLibrary);
  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kIOLibrary);
  dart::bin::Builtin::SetNativeResolver(dart::bin::Builtin::kCLILibrary);
  dart::bin::VmService::SetNativeResolver();

  // DIFF(main_impl): this embedder does not parse CLI args, so Options::namespc
  // and Options::exit_disabled take default values unless the embedder sets
  // them elsewhere.
  const char* namespc = is_kernel_isolate ? nullptr : dart::bin::Options::namespc();
  result = dart::bin::DartUtils::SetupIOLibrary(
      namespc, script_uri, dart::bin::Options::exit_disabled());
  if (Dart_IsError(result)) {
    return result;
  }

  return Dart_Null();
}

// init callback
static bool OnIsolateInitialize(void** child_callback_data, char** error) {
  if (child_callback_data != nullptr) {
    *child_callback_data = nullptr;
  }
  if (error != nullptr) {
    *error = nullptr;
  }

  Dart_Isolate isolate = Dart_CurrentIsolate();
  ASSERT(isolate != nullptr);

  auto* isolate_group_data = reinterpret_cast<dart::bin::IsolateGroupData*>(
      Dart_CurrentIsolateGroupData());
  if (isolate_group_data == nullptr) {
    SetErrorIfUnset(error, "OnIsolateInitialize: isolate_group_data is null.");
    return false;
  }

  auto* isolate_data = new dart::bin::IsolateData(isolate_group_data);
  if (child_callback_data != nullptr) {
    *child_callback_data = isolate_data;
  }

  Dart_EnterScope();
  const char* script_uri = isolate_group_data->script_url;
  if (script_uri == nullptr) {
    script_uri = "";
  }

  Dart_Handle result = SetupCoreLibraries(
    isolate, isolate_data,
    /*is_isolate_group_start=*/false,
    /*is_kernel_isolate=*/false,
    /*resolved_packages_config=*/nullptr);
  if (SetErrorFromHandle(result, error)) goto failed;

  if (isolate_group_data->RunFromAppSnapshot()) {
    result = dart::bin::Loader::InitForSnapshot(script_uri, isolate_data);
    if (SetErrorFromHandle(result, error)) goto failed;
  } else {
    result = dart::bin::DartUtils::ResolveScript(Dart_NewStringFromCString(script_uri));
    if (SetErrorFromHandle(result, error)) goto failed;

    if (isolate_group_data->kernel_buffer() != nullptr) {
      const char* resolved_script_uri = nullptr;
      result = Dart_StringToCString(result, &resolved_script_uri);
      if (SetErrorFromHandle(result, error)) goto failed;
      result = dart::bin::Loader::InitForSnapshot(resolved_script_uri, isolate_data);
      if (SetErrorFromHandle(result, error)) goto failed;
    }
  }

  Dart_ExitScope();
  return true;

failed:
  Dart_ExitScope();
  if (child_callback_data != nullptr) {
    *child_callback_data = nullptr;
  }
  delete isolate_data;
  return false;
}

#if defined(DARTVM_EMBED_ENABLE_NATIVE_ASSETS)
static void* NativeAssetsDlopenRelative(const char* path, char** error) {
  auto* isolate_group_data = reinterpret_cast<dart::bin::IsolateGroupData*>(
      Dart_CurrentIsolateGroupData());
  const char* script_uri =
      (isolate_group_data != nullptr) ? isolate_group_data->script_url : nullptr;
  return dart::bin::NativeAssets::DlopenRelative(path, script_uri, error);
}
#endif  // defined(DARTVM_EMBED_ENABLE_NATIVE_ASSETS)

static bool FileModifiedCallbackTrampoline(const char* url, int64_t since) {
  if (State().file_modified_callback != nullptr) {
    return State().file_modified_callback(url, since);
  }
  return FileModifiedCallback(url, since);
}

static Dart_Isolate IsolateSetupHelper(Dart_Isolate isolate,
                                       bool is_main_isolate,
                                       const char* script_uri,
                                       const char* packages_config,
                                       bool isolate_run_app_snapshot,
                                       Dart_IsolateFlags* flags,
                                       char** error) {
  (void)packages_config;  // DIFF(main_impl): only used for exit_code paths.
  Dart_EnterScope();

  // Set up the library tag handler for the isolate group shared by all
  // isolates in the group.
  Dart_Handle result =
      Dart_SetLibraryTagHandler(dart::bin::Loader::LibraryTagHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
  }
  result = Dart_SetDeferredLoadHandler(dart::bin::Loader::DeferredLoadHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
  }

  auto* isolate_data =
      reinterpret_cast<dart::bin::IsolateData*>(Dart_IsolateData(isolate));
  if (isolate_data == nullptr) {
    SetErrorIfUnset(error, "IsolateSetupHelper: isolate_data is null.");
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
  }

  const char* resolved_packages_config = nullptr;
  result = SetupCoreLibraries(isolate, isolate_data,
                              /*is_isolate_group_start=*/true,
                              /*is_kernel_isolate=*/(flags != nullptr) &&
                                  flags->is_kernel_isolate,
                              &resolved_packages_config);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
  }

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  auto* isolate_group_data = isolate_data->isolate_group_data();
  uint8_t* kernel_buffer = isolate_group_data->kernel_buffer().get();
  intptr_t kernel_buffer_size = isolate_group_data->kernel_buffer_size();

  // Align with runtime/bin/main_impl.cc: compile to kernel on demand when
  // starting from sources and the isolate group didn't already carry a kernel.
  if (!isolate_run_app_snapshot && kernel_buffer == nullptr &&
      !Dart_IsKernelIsolate(isolate)) {
    if (!dart::bin::dfe.CanUseDartFrontend()) {
      // DIFF(main_impl): main_impl reports an exit_code; library API only
      // returns an error string.
      SetErrorIfUnset(error, "Dart frontend unavailable to compile script.");
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
    uint8_t* application_kernel_buffer = nullptr;
    intptr_t application_kernel_buffer_size = 0;
    // Match main_impl: for_snapshot only when generating app-jit snapshot.
    const bool for_snapshot =
        dart::bin::Options::gen_snapshot_kind() == dart::bin::kAppJIT;
    const bool embed_sources =
        dart::bin::Options::gen_snapshot_kind() != dart::bin::kAppJIT;
    int compile_exit_code = 0;
    dart::bin::dfe.CompileAndReadScript(
        script_uri, &application_kernel_buffer, &application_kernel_buffer_size,
        /*error=*/error,
        &compile_exit_code,
        resolved_packages_config, for_snapshot, embed_sources);
    if (application_kernel_buffer == nullptr) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
    isolate_group_data->SetKernelBufferNewlyOwned(application_kernel_buffer,
                                                  application_kernel_buffer_size);
    kernel_buffer = application_kernel_buffer;
    kernel_buffer_size = application_kernel_buffer_size;
  }

  if (kernel_buffer != nullptr) {
    Dart_Handle uri = Dart_NewStringFromCString(script_uri);
    if (SetErrorFromHandle(uri, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
    Dart_Handle resolved_script_uri = dart::bin::DartUtils::ResolveScript(uri);
    if (SetErrorFromHandle(resolved_script_uri, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
    result = Dart_LoadScriptFromKernel(kernel_buffer, kernel_buffer_size);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
  }
#endif  // !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)

  if (isolate_run_app_snapshot) {
    result = dart::bin::Loader::InitForSnapshot(script_uri, isolate_data);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
    if (is_main_isolate) {
      // Mirrors main_impl: cache canonical script uri for spawnUri decisions.
      const char* resolved_script_uri = nullptr;
      result = Dart_StringToCString(
          dart::bin::DartUtils::ResolveScript(
              Dart_NewStringFromCString(script_uri)),
          &resolved_script_uri);
      if (SetErrorFromHandle(result, error)) {
        Dart_ExitScope();
        Dart_ShutdownIsolate();
        return nullptr;
      }
      State().app_script_uri = resolved_script_uri != nullptr ? resolved_script_uri : "";
    }
#endif  // !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  } else {
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
    auto* isolate_group_data = isolate_data->isolate_group_data();
    if (isolate_group_data->kernel_buffer() != nullptr) {
      Dart_Handle uri =
          dart::bin::DartUtils::ResolveScript(Dart_NewStringFromCString(script_uri));
      if (SetErrorFromHandle(uri, error)) {
        Dart_ExitScope();
        Dart_ShutdownIsolate();
        return nullptr;
      }
      const char* resolved_script_uri = nullptr;
      result = Dart_StringToCString(uri, &resolved_script_uri);
      if (SetErrorFromHandle(result, error)) {
        Dart_ExitScope();
        Dart_ShutdownIsolate();
        return nullptr;
      }
      result = dart::bin::Loader::InitForSnapshot(resolved_script_uri, isolate_data);
      if (SetErrorFromHandle(result, error)) {
        Dart_ExitScope();
        Dart_ShutdownIsolate();
        return nullptr;
      }
    }
    Dart_RecordTimelineEvent("LoadScript", Dart_TimelineGetMicros(),
                             Dart_GetMainPortId(), /*flow_id_count=*/0, nullptr,
                             Dart_Timeline_Event_Async_End,
                             /*argument_count=*/0, nullptr, nullptr);
#else
    // Precompiled runtime does not load scripts from kernel here.
#endif
  }

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if ((dart::bin::Options::gen_snapshot_kind() == dart::bin::kAppJIT) &&
      is_main_isolate) {
    result = Dart_SortClasses();
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return nullptr;
    }
  }
#endif

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // Disable pausing the DartDev isolate on start and exit.
  const char* isolate_name = nullptr;
  result = Dart_StringToCString(Dart_DebugName(), &isolate_name);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return nullptr;
  }
  if (isolate_name != nullptr &&
      strstr(isolate_name, DART_DEV_ISOLATE_NAME) != nullptr) {
    Dart_SetShouldPauseOnStart(false);
    Dart_SetShouldPauseOnExit(false);
  }
#endif  // !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)

#if !defined(DART_PRECOMPILER) && defined(DARTVM_EMBED_ENABLE_NATIVE_ASSETS)
  // DIFF(main_impl): the embedder-runtime static libraries used by this
  // project may not include NativeAssets symbols. Opt-in via
  // DARTVM_EMBED_ENABLE_NATIVE_ASSETS when available.
  NativeAssetsApi native_assets;
  memset(&native_assets, 0, sizeof(native_assets));
  native_assets.dlopen_absolute = &dart::bin::NativeAssets::DlopenAbsolute;
  native_assets.dlopen_relative = &NativeAssetsDlopenRelative;
  native_assets.dlopen_system = &dart::bin::NativeAssets::DlopenSystem;
  native_assets.dlopen_executable = &dart::bin::NativeAssets::DlopenExecutable;
  native_assets.dlopen_process = &dart::bin::NativeAssets::DlopenProcess;
  native_assets.dlsym = &dart::bin::NativeAssets::Dlsym;
  Dart_InitializeNativeAssetsResolver(&native_assets);
#endif  // !defined(DART_PRECOMPILER) && defined(DARTVM_EMBED_ENABLE_NATIVE_ASSETS)

  // Make the isolate runnable so that it is ready to handle messages.
  Dart_ExitScope();
  Dart_ExitIsolate();
  char* make_runnable_error = Dart_IsolateMakeRunnable(isolate);
  if (make_runnable_error != nullptr) {
    if (error != nullptr) {
      *error = make_runnable_error;
    } else {
      free(make_runnable_error);
    }
    Dart_EnterIsolate(isolate);
    Dart_ShutdownIsolate();
    return nullptr;
  }

  return isolate;
}

static Dart_Isolate CreateAndSetupKernelIsolate(const char *script_uri,
                                                const char *packages_config,
                                                Dart_IsolateFlags *flags,
                                                char **error) {
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  (void)script_uri;
  (void)packages_config;
  (void)flags;
  SetErrorIfUnset(error,
                  "CreateAndSetupKernelIsolate is unavailable in precompiled "
                  "runtime.");
  return nullptr;
#else
  // DIFF(main_impl): main_impl skips kernel isolate start for app-jit
  // snapshot generation and has additional bookkeeping; this library always
  // allows kernel isolate creation when requested by the VM.
  const char *kernel_snapshot_uri = dart::bin::dfe.frontend_filename();
  const char *uri =
      kernel_snapshot_uri != nullptr ? kernel_snapshot_uri : script_uri;
  std::string sanitized_kernel_uri_storage;
  const char *sanitized_kernel_uri =
      SanitizePathLikeMain(uri, &sanitized_kernel_uri_storage);

  Dart_Isolate isolate = nullptr;
  dart::bin::IsolateGroupData* isolate_group_data = nullptr;
  dart::bin::IsolateData* isolate_data = nullptr;
  bool isolate_run_app_snapshot = false;
  dart::bin::AppSnapshot* app_snapshot = nullptr;

  // Kernel isolate can start from an app-jit snapshot of the frontend (if
  // provided) or fall back to the kernel-service dill. Mirrors main_impl.
  if ((kernel_snapshot_uri != nullptr) &&
      ((app_snapshot = dart::bin::Snapshot::TryReadAppSnapshot(
            kernel_snapshot_uri, /*force_load_elf_from_memory=*/false,
            /*decode_uri=*/false)) != nullptr) &&
      app_snapshot->IsJIT()) {
    const uint8_t* isolate_snapshot_data = nullptr;
    const uint8_t* isolate_snapshot_instructions = nullptr;
    const uint8_t* ignore_vm_snapshot_data = nullptr;
    const uint8_t* ignore_vm_snapshot_instructions = nullptr;
    isolate_run_app_snapshot = true;
         
    app_snapshot->SetBuffers(&ignore_vm_snapshot_data,
                             &ignore_vm_snapshot_instructions,
                             &isolate_snapshot_data,
                             &isolate_snapshot_instructions);

    isolate_group_data = new dart::bin::IsolateGroupData(
        sanitized_kernel_uri, packages_config, app_snapshot, isolate_run_app_snapshot);
    isolate_data = new dart::bin::IsolateData(isolate_group_data);
    isolate = Dart_CreateIsolateGroup(
        DART_KERNEL_ISOLATE_NAME, DART_KERNEL_ISOLATE_NAME,
        isolate_snapshot_data, isolate_snapshot_instructions, flags,
        isolate_group_data, isolate_data, error);
  }

  if (isolate == nullptr) {
    // Clear error from app snapshot attempt and fall back to kernel service.
    if (error != nullptr && *error != nullptr) {
      free(*error);
      *error = nullptr;
    }
    delete isolate_data;
    delete isolate_group_data;
    isolate_data = nullptr;
    isolate_group_data = nullptr;
    if (app_snapshot != nullptr) {
      delete app_snapshot;
      app_snapshot = nullptr;
    }

    const uint8_t *kernel_service_buffer = nullptr;
    intptr_t kernel_service_buffer_size = 0;
    dart::bin::dfe.LoadKernelService(&kernel_service_buffer,
                                     &kernel_service_buffer_size);
    if (kernel_service_buffer == nullptr || kernel_service_buffer_size <= 0) {
      SetErrorIfUnset(
          error, "CreateAndSetupKernelIsolate: failed to load kernel-service "
                 "kernel binary.");
      return nullptr;
    }

    isolate_group_data = new dart::bin::IsolateGroupData(
        sanitized_kernel_uri, packages_config, nullptr,
        isolate_run_app_snapshot);
    isolate_group_data->SetKernelBufferUnowned(
        const_cast<uint8_t*>(kernel_service_buffer),
        kernel_service_buffer_size);
    isolate_data = new dart::bin::IsolateData(isolate_group_data);
    isolate = Dart_CreateIsolateGroupFromKernel(
        DART_KERNEL_ISOLATE_NAME, DART_KERNEL_ISOLATE_NAME,
        kernel_service_buffer, kernel_service_buffer_size, flags,
        isolate_group_data, isolate_data, error);
  }

  if (isolate == nullptr) {
    delete isolate_data;
    delete isolate_group_data;
    return nullptr;
  }

  Dart_Isolate created = IsolateSetupHelper(
      isolate, /*is_main_isolate=*/false, uri, packages_config,
      isolate_run_app_snapshot, flags, error);
  if (created == nullptr) {
    delete isolate_data;
    delete isolate_group_data;
    return nullptr;
  }

  State().callback_owned_isolate_data.insert(isolate_data);
  State().callback_owned_isolate_group_data.insert(isolate_group_data);
  return created;
#endif
}

static Dart_Isolate CreateAndSetupServiceIsolate(const char *script_uri,
                                                 const char *packages_config,
                                                 Dart_IsolateFlags *flags,
                                                 char **error) {
  ASSERT(script_uri != nullptr);
  ASSERT(flags != nullptr);

  Dart_Isolate isolate = nullptr;
  auto* isolate_group_data =
      new dart::bin::IsolateGroupData(script_uri, packages_config, nullptr,
                                      /*isolate_run_app_snapshot=*/false);

  const uint8_t* isolate_snapshot_data = nullptr;
  const uint8_t* isolate_snapshot_instructions = nullptr;

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // AOT: service isolate is included in the app snapshot in non-PRODUCT mode.
  isolate_snapshot_data = State().app_isolate_snapshot_data;
  isolate_snapshot_instructions = State().app_isolate_snapshot_instructions;
  if (isolate_snapshot_data == nullptr || isolate_snapshot_instructions == nullptr) {
    // DIFF(main_impl): main_impl guarantees these are set before VM init.
    SetErrorIfUnset(error, "Service isolate requested but app snapshot buffers are unset.");
    delete isolate_group_data;
    return nullptr;
  }
  isolate = Dart_CreateIsolateGroup(
      script_uri, DART_VM_SERVICE_ISOLATE_NAME, isolate_snapshot_data,
      isolate_snapshot_instructions, flags, isolate_group_data,
      /*isolate_data=*/nullptr, error);
#else
  // JIT: service isolate uses the core libraries snapshot.
  flags->load_vmservice_library = true;
  flags->null_safety = true;
  isolate_snapshot_data = kDartCoreIsolateSnapshotData;
  isolate_snapshot_instructions = kDartCoreIsolateSnapshotInstructions;
  isolate = Dart_CreateIsolateGroup(
      script_uri, DART_VM_SERVICE_ISOLATE_NAME, isolate_snapshot_data,
      isolate_snapshot_instructions, flags, isolate_group_data,
      /*isolate_data=*/nullptr, error);
#endif
  if (isolate == nullptr) {
    delete isolate_group_data;
    return nullptr;
  }

  Dart_EnterScope();
  Dart_Handle result = Dart_SetLibraryTagHandler(dart::bin::Loader::LibraryTagHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    delete isolate_group_data;
    return nullptr;
  }
  result = Dart_SetDeferredLoadHandler(dart::bin::Loader::DeferredLoadHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    delete isolate_group_data;
    return nullptr;
  }

  // DIFF(main_impl): main_impl uses Options::* derived from CLI parsing.
  // Here we use embedder-controlled State() and keep most toggles at defaults.
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // AOT runtime does not support the VM service. The VM may request a service
  // isolate (non-PRODUCT AOT snapshots embed dart:vmservice), but we skip the
  // HTTP listener since debugging/observability is unavailable in AOT.
#else
  const bool wait_for_dds_to_advertise_service = false;
  const bool serve_devtools = true;
  if (!dart::bin::VmService::Setup(
          State().vm_service_ip.c_str(), State().vm_service_port,
          /*dev_mode_server=*/false, State().vm_service_auth_codes_disabled,
          /*write_service_info_filename=*/nullptr,
          dart::bin::Options::trace_loading(),
          dart::bin::Options::deterministic(),
          /*enable_service_port_fallback=*/true,
          wait_for_dds_to_advertise_service,
          serve_devtools,
          /*serve_observatory=*/true,
          /*print_dtd=*/false,
          /*should_use_resident_compiler=*/false,
          /*resident_compiler_info_file_path=*/nullptr)) {
    SetErrorIfUnset(error, dart::bin::VmService::GetErrorMessage());
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    delete isolate_group_data;
    return nullptr;
  }
#endif

  if (dart::bin::Options::compile_all()) {
    result = Dart_CompileAll();
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      delete isolate_group_data;
      return nullptr;
    }
  }

  result = Dart_SetEnvironmentCallback(dart::bin::DartUtils::EnvironmentCallback);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    delete isolate_group_data;
    return nullptr;
  }

  Dart_ExitScope();
  Dart_ExitIsolate();
  State().callback_owned_isolate_group_data.insert(isolate_group_data);

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // Ensure the AOT snapshot backing store isn't unloaded while the service
  // isolate is alive.
  if (State().app_aot_elf_handle != nullptr) {
    State().isolate_loaded_aot_elfs[isolate] = State().app_aot_elf_handle;
    State().app_aot_elf_refcount++;
  }
#endif
  return isolate;
}

static Dart_Isolate CreateIsolateGroupAndSetupHelper(bool is_main_isolate,
                                                     const char* script_uri,
                                                     const char* name,
                                                     const char* packages_config,
                                                     Dart_IsolateFlags* flags,
                                                     void* callback_data,
                                                     char** error) {
  (void)callback_data;
  const uint8_t* isolate_snapshot_data = nullptr;
  const uint8_t* isolate_snapshot_instructions = nullptr;
  uint8_t* kernel_buffer = nullptr;
  intptr_t kernel_buffer_size = 0;
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  dart::bin::AppSnapshot* app_snapshot = nullptr;

  // JIT: default to core snapshot; optionally use app-jit snapshot when:
  // - embedder previously started the main isolate from an app snapshot, or
  // - spawnUri points directly at an app snapshot file.
  bool isolate_run_app_snapshot = false;
  isolate_snapshot_data = kDartCoreIsolateSnapshotData;
  isolate_snapshot_instructions = kDartCoreIsolateSnapshotInstructions;
  if ((State().app_isolate_snapshot_data != nullptr) &&
      (is_main_isolate ||
       (!State().app_script_uri.empty() &&
        strcmp(script_uri, State().app_script_uri.c_str()) == 0))) {
    isolate_run_app_snapshot = true;
    isolate_snapshot_data = State().app_isolate_snapshot_data;
    isolate_snapshot_instructions = State().app_isolate_snapshot_instructions;
  } else if (!is_main_isolate) {
    app_snapshot = dart::bin::Snapshot::TryReadAppSnapshot(script_uri);
    if (app_snapshot != nullptr && app_snapshot->IsJITorAOT()) {
      if (app_snapshot->IsAOT()) {
        SetErrorIfUnset(error,
                        "spawnUri provided an AOT snapshot; JIT VM cannot use it.");
        delete app_snapshot;
        return nullptr;
      }
      isolate_run_app_snapshot = true;
      const uint8_t* ignore_vm_snapshot_data = nullptr;
      const uint8_t* ignore_vm_snapshot_instructions = nullptr;
      app_snapshot->SetBuffers(&ignore_vm_snapshot_data,
                               &ignore_vm_snapshot_instructions,
                               &isolate_snapshot_data,
                               &isolate_snapshot_instructions);
    }
  }

  if (kernel_buffer == nullptr && !isolate_run_app_snapshot) {
    dart::bin::dfe.ReadScript(script_uri, app_snapshot, &kernel_buffer,
                              &kernel_buffer_size, /*decode_uri=*/true);
  }
#else
  // AOT: all isolates must be spawned from AOT compiled snapshots.
  dart::bin::AppSnapshot* app_snapshot = nullptr;
  bool isolate_run_app_snapshot = true;
  if (is_main_isolate) {
    isolate_snapshot_data = State().app_isolate_snapshot_data;
    isolate_snapshot_instructions = State().app_isolate_snapshot_instructions;
  } else {
    app_snapshot = dart::bin::Snapshot::TryReadAppSnapshot(script_uri, false);
    if (app_snapshot == nullptr || !app_snapshot->IsAOT()) {
      SetErrorIfUnset(
          error,
          "spawnUri provided uri does not contain a valid AOT snapshot.");
      delete app_snapshot;
      return nullptr;
    }
    const uint8_t* ignore_vm_snapshot_data = nullptr;
    const uint8_t* ignore_vm_snapshot_instructions = nullptr;
    app_snapshot->SetBuffers(&ignore_vm_snapshot_data,
                             &ignore_vm_snapshot_instructions,
                             &isolate_snapshot_data,
                             &isolate_snapshot_instructions);
  }
#endif  // !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)

  auto* isolate_group_data =
      new dart::bin::IsolateGroupData(script_uri, packages_config, app_snapshot,
                                      isolate_run_app_snapshot);
  if (kernel_buffer != nullptr) {
    isolate_group_data->SetKernelBufferNewlyOwned(kernel_buffer, kernel_buffer_size);
  }

  Dart_Isolate isolate = nullptr;
  auto* isolate_data = new dart::bin::IsolateData(isolate_group_data);

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (!isolate_run_app_snapshot && (isolate_snapshot_data == nullptr)) {
    const uint8_t* platform_kernel_buffer = nullptr;
    intptr_t platform_kernel_buffer_size = 0;
    dart::bin::dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
    if (platform_kernel_buffer == nullptr) {
      platform_kernel_buffer = kernel_buffer;
      platform_kernel_buffer_size = kernel_buffer_size;
    }
    if (platform_kernel_buffer == nullptr) {
      SetErrorIfUnset(error, "platform_program cannot be nullptr.");
      delete isolate_data;
      delete isolate_group_data;
      return nullptr;
    }
    isolate = Dart_CreateIsolateGroupFromKernel(
        script_uri, name, platform_kernel_buffer, platform_kernel_buffer_size,
        flags, isolate_group_data, isolate_data, error);
  } else {
    isolate = Dart_CreateIsolateGroup(script_uri, name, isolate_snapshot_data,
                                      isolate_snapshot_instructions, flags,
                                      isolate_group_data, isolate_data, error);
  }
#else
  isolate = Dart_CreateIsolateGroup(script_uri, name, isolate_snapshot_data,
                                    isolate_snapshot_instructions, flags,
                                    isolate_group_data, isolate_data, error);
#endif

  Dart_Isolate created_isolate = nullptr;
  if (isolate == nullptr) {
    delete isolate_data;
    delete isolate_group_data;
  } else {
    created_isolate = IsolateSetupHelper(isolate, is_main_isolate, script_uri,
                                         packages_config, isolate_run_app_snapshot,
                                         flags, error);
  }
  if (created_isolate != nullptr) {
    State().callback_owned_isolate_data.insert(isolate_data);
    State().callback_owned_isolate_group_data.insert(isolate_group_data);
  }
  return created_isolate;
}

static Dart_Isolate CreateIsolateGroupAndSetup(const char *script_uri,
                                         const char *main,
                                         const char *package_root,
                                         const char *package_config,
                                         Dart_IsolateFlags *flags,
                                         void *callback_data, char **error) {
  (void)callback_data;
  if (error != nullptr) {
    *error = nullptr;
  }
  // The VM should never call the isolate helper with a nullptr flags.
  ASSERT(flags != nullptr);
  ASSERT(flags->version == DART_FLAGS_CURRENT_VERSION);
  ASSERT(package_root == nullptr);
  if (script_uri == nullptr) {
    SetErrorIfUnset(error, "OnCreateIsolateGroup: script_uri is null.");
    return nullptr;
  }

  bool dontneed_safe = true;
#if defined(DART_HOST_OS_LINUX)
  // Mirrors main_impl comment: dontneed_safe defaults differ on some Linux
  // environments.
  dontneed_safe = false;
#elif defined(DEBUG)
  // If the snapshot isn't file-backed, madvise(DONT_NEED) is destructive.
  if (dart::bin::Options::force_load_elf_from_memory()) {
    dontneed_safe = false;
  }
#endif
  flags->snapshot_is_dontneed_safe = dontneed_safe;

  const char *effective_name = (main != nullptr) ? main : script_uri;
  const char *effective_packages_config =
      EffectivePackagesConfig(package_config);

  std::string sanitized_script_uri_storage;
  std::string sanitized_packages_config_storage;
  const char *sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);
  const char *sanitized_packages_config = SanitizePathLikeMain(
      effective_packages_config, &sanitized_packages_config_storage);

  if (strcmp(script_uri, DART_KERNEL_ISOLATE_NAME) == 0) {
    return CreateAndSetupKernelIsolate(script_uri, sanitized_packages_config,
                                       flags, error);
  }
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (strcmp(script_uri, DART_DEV_ISOLATE_NAME) == 0) {
    // DIFF(main_impl): main_impl can start DartDev isolate based on CLI state.
    // Here we only honor VM requests to create it.
    // If DartDev cannot be resolved, surface a clear error.
    int64_t start = Dart_TimelineGetMicros();
    auto dartdev_path = dart::bin::DartDevIsolate::TryResolveDartDevSnapshotPath();
    if (dartdev_path.get() == nullptr) {
      SetErrorIfUnset(error,
                      "Failed to start DartDev isolate: could not resolve snapshot/kernel.");
      return nullptr;
    }
    Dart_IsolateFlags local_flags = *flags;
    const uint8_t* isolate_snapshot_data = kDartCoreIsolateSnapshotData;
    const uint8_t* isolate_snapshot_instructions =
        kDartCoreIsolateSnapshotInstructions;
    dart::bin::IsolateGroupData* isolate_group_data = nullptr;
    dart::bin::IsolateData* isolate_data = nullptr;
    dart::bin::AppSnapshot* app_snapshot = nullptr;
    bool isolate_run_app_snapshot = true;
    Dart_Isolate isolate = nullptr;

    if (((app_snapshot = dart::bin::Snapshot::TryReadAppSnapshot(
              dartdev_path.get(), /*force_load_elf_from_memory=*/false,
              /*decode_uri=*/false)) != nullptr) &&
        app_snapshot->IsJIT()) {
      const uint8_t* dd_isolate_snapshot_data = nullptr;
      const uint8_t* dd_isolate_snapshot_instructions = nullptr;
      const uint8_t* ignore_vm_snapshot_data = nullptr;
      const uint8_t* ignore_vm_snapshot_instructions = nullptr;
      app_snapshot->SetBuffers(&ignore_vm_snapshot_data,
                               &ignore_vm_snapshot_instructions,
                               &dd_isolate_snapshot_data,
                               &dd_isolate_snapshot_instructions);
      isolate_group_data = new dart::bin::IsolateGroupData(
          DART_DEV_ISOLATE_NAME, sanitized_packages_config, app_snapshot,
          isolate_run_app_snapshot);
      isolate_data = new dart::bin::IsolateData(isolate_group_data);
      isolate = Dart_CreateIsolateGroup(
          DART_DEV_ISOLATE_NAME, DART_DEV_ISOLATE_NAME, dd_isolate_snapshot_data,
          dd_isolate_snapshot_instructions, &local_flags, isolate_group_data,
          isolate_data, error);
    }

    if (isolate == nullptr) {
      if (error != nullptr && *error != nullptr) {
        free(*error);
        *error = nullptr;
      }
      isolate_run_app_snapshot = false;
      if (app_snapshot != nullptr) {
        delete app_snapshot;
        app_snapshot = nullptr;
      }
      isolate_group_data = new dart::bin::IsolateGroupData(
          DART_DEV_ISOLATE_NAME, sanitized_packages_config, nullptr,
          isolate_run_app_snapshot);
      uint8_t* application_kernel_buffer = nullptr;
      intptr_t application_kernel_buffer_size = 0;
      dart::bin::dfe.ReadScript(dartdev_path.get(), nullptr,
                                &application_kernel_buffer,
                                &application_kernel_buffer_size,
                                /*decode_uri=*/false);
      isolate_group_data->SetKernelBufferNewlyOwned(application_kernel_buffer,
                                                    application_kernel_buffer_size);
      isolate_data = new dart::bin::IsolateData(isolate_group_data);
      isolate = Dart_CreateIsolateGroup(
          DART_DEV_ISOLATE_NAME, DART_DEV_ISOLATE_NAME, isolate_snapshot_data,
          isolate_snapshot_instructions, &local_flags, isolate_group_data,
          isolate_data, error);
    }

    Dart_Isolate created = nullptr;
    if (isolate != nullptr) {
      created = IsolateSetupHelper(isolate, /*is_main_isolate=*/false,
                                   DART_DEV_ISOLATE_NAME, sanitized_packages_config,
                                   isolate_run_app_snapshot, &local_flags, error);
    }
    if (created == nullptr) {
      delete isolate_data;
      delete isolate_group_data;
      return nullptr;
    }
    State().callback_owned_isolate_data.insert(isolate_data);
    State().callback_owned_isolate_group_data.insert(isolate_group_data);
    int64_t end = Dart_TimelineGetMicros();
    Dart_RecordTimelineEvent("CreateAndSetupDartDevIsolate", start, end,
                             /*flow_id_count=*/0, nullptr,
                             Dart_Timeline_Event_Duration,
                             /*argument_count=*/0, nullptr, nullptr);
    return created;
  }
#endif  // !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (strcmp(script_uri, DART_VM_SERVICE_ISOLATE_NAME) == 0) {
    return CreateAndSetupServiceIsolate(
        sanitized_script_uri, sanitized_packages_config, flags, error);
  }
  const bool is_main_isolate = false;
  return CreateIsolateGroupAndSetupHelper(is_main_isolate, sanitized_script_uri,
                                          effective_name, sanitized_packages_config,
                                          flags, callback_data, error);
}

static void OnIsolateShutdown(void *isolate_group_data, void *isolate_data) {
  (void)isolate_group_data;
  (void)isolate_data;
  Dart_EnterScope();
  Dart_Handle sticky_error = Dart_GetStickyError();
  if (!Dart_IsNull(sticky_error) && !Dart_IsFatalError(sticky_error)) {
    // Align with main_impl: report via Syslog.
    dart::Syslog::PrintErr("%s\n", Dart_GetError(sticky_error));
  }
  Dart_ExitScope();
}

static void DeleteIsolateData(void *isolate_group_data, void *callback_data) {
  (void)isolate_group_data;
  auto *isolate_data =
      reinterpret_cast<dart::bin::IsolateData *>(callback_data);
  if (isolate_data == nullptr) {
    return;
  }
  // DIFF(main_impl): main_impl always deletes isolate data because it always
  // owns it. This library supports caller-provided isolate data for public
  // creation APIs, so we only delete if it was created by our callbacks.
  if (State().callback_owned_isolate_data.erase(
          isolate_data) > 0) {
    delete isolate_data;
  }
}

static void DeleteIsolateGroupData(void *callback_data) {
  auto *group_data =
      reinterpret_cast<dart::bin::IsolateGroupData *>(callback_data);
  if (group_data == nullptr) {
    return;
  }
  // DIFF(main_impl): main_impl always deletes isolate group data because it
  // always owns it. This library supports caller-provided group data for
  // public creation APIs, so we only delete if it was created by our callbacks.
  if (State().callback_owned_isolate_group_data.erase(group_data) >
      0) {
    delete group_data;
  }
}

// apis
extern "C" {

bool DartVmEmbed_Initialize(const DartVmEmbedInitConfig* config, char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (State().vm_initialized) {
    return true;
  }

  if (const char* ip = getenv("DARTVM_EMBED_VM_SERVICE_IP")) {
    if (ip[0] != '\0') {
      State().vm_service_ip = ip;
    }
  }
  if (const char* port = getenv("DARTVM_EMBED_VM_SERVICE_PORT")) {
    const int value = atoi(port);
    if (value > 0) {
      State().vm_service_port = value;
    }
  }
  if (const char* auth = getenv("DARTVM_EMBED_VM_SERVICE_AUTH_CODES_DISABLED")) {
    State().vm_service_auth_codes_disabled = (strcmp(auth, "0") != 0);
  }

  // DIFF(main_impl): no CLI Options parsing here; caller passes flags in config.
  std::vector<const char*> vm_flags;
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  vm_flags.push_back("--precompilation");
#endif
  // DIFF(main_impl): `--enable-vm-service` and related switches are *dart*
  // (main_options) flags, not VM flags. main_impl parses them before calling
  // Dart_SetVMFlags(). This library currently relies on isolate flags +
  // CreateAndSetupServiceIsolate(VmService::Setup) for service startup.
  if (config != nullptr && config->vm_flag_count > 0 && config->vm_flags != nullptr) {
    for (int i = 0; i < config->vm_flag_count; ++i) {
      const char* flag = config->vm_flags[i];
      vm_flags.push_back(flag);
    }
  }

  char* embedder_error = nullptr;
  if (!dart::embedder::InitOnce(&embedder_error)) {
    if (error != nullptr) {
      *error = DupMessage(embedder_error);
    }
    free(embedder_error);
    return false;
  }

  // Keep ordering aligned with runtime/bin/main_impl.cc: VM flags before DFE.
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

  dart::bin::Loader::InitOnce();

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  dart::bin::dfe.Init();
  dart::bin::dfe.set_use_dfe();
  dart::bin::dfe.set_use_incremental_compiler(true);
  // DIFF(main_impl): main_impl also sets verbosity based on parsed CLI and may
  // pre-read the application script to seed dfe.application_kernel_buffer.
  dart::bin::dfe.set_verbosity(dart::bin::Options::verbosity_level());
#endif

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
  State().vm_snapshot_data = params.vm_snapshot_data;
  State().vm_snapshot_instructions = params.vm_snapshot_instructions;

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  params.start_kernel_isolate = false;
#else
  const bool dfe_available =
      dart::bin::dfe.UseDartFrontend() && dart::bin::dfe.CanUseDartFrontend();
  const bool requested =
      (config == nullptr) ? true : config->start_kernel_isolate;
  // Match main_impl: only start kernel isolate when DFE is usable.
  params.start_kernel_isolate = requested && dfe_available;
#endif

  params.create_group = CreateIsolateGroupAndSetup;  // main_impl: CreateIsolateGroupAndSetup
  params.initialize_isolate = OnIsolateInitialize;   // main_impl: OnIsolateInitialize
  params.shutdown_isolate = OnIsolateShutdown;       // main_impl: OnIsolateShutdown
  params.cleanup_isolate = DeleteIsolateData;        // main_impl: DeleteIsolateData
  params.cleanup_group = DeleteIsolateGroupData;     // main_impl: DeleteIsolateGroupData

  params.file_open = dart::bin::DartUtils::OpenFile;
  params.file_read = dart::bin::DartUtils::ReadFile;
  params.file_write = dart::bin::DartUtils::WriteFile;
  params.file_close = dart::bin::DartUtils::CloseFile;
  params.entropy_source = dart::bin::DartUtils::EntropySource;

  // DIFF(main_impl): get_service_assets not provided here (Observatory UI assets).
  params.get_service_assets = nullptr;

  char* init_error = Dart_Initialize(&params);
  if (init_error != nullptr) {
    dart::embedder::Cleanup();
    if (error != nullptr) {
      *error = DupMessage(init_error);
    }
    free(init_error);
    return false;
  }

  Dart_SetServiceStreamCallbacks(&ServiceStreamListenCallback,
                                 &ServiceStreamCancelCallback);
  char* file_modified_error = Dart_SetFileModifiedCallback(FileModifiedCallbackTrampoline);
  if (file_modified_error != nullptr) {
    if (error != nullptr) {
      *error = DupMessage(file_modified_error);
    }
    free(file_modified_error);
    char* cleanup_error = Dart_Cleanup();
    if (cleanup_error != nullptr) {
      free(cleanup_error);
    }
    dart::embedder::Cleanup();
    return false;
  }

  Dart_SetEmbedderInformationCallback([](Dart_EmbedderInformation* info) {
    info->version = DART_EMBEDDER_INFORMATION_CURRENT_VERSION;
    info->name = "DartVmEmbed";
    dart::bin::Process::GetRSSInformation(&(info->max_rss), &(info->current_rss));
  });

  State().vm_initialized = true;
  return true;
}

bool DartVmEmbed_Cleanup(char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (!State().vm_initialized) {
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
  State().vm_initialized = false;
  State().vm_snapshot_data = nullptr;
  State().vm_snapshot_instructions = nullptr;
  // Release snapshot backing stores we keep across isolates.
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (State().app_aot_elf_handle != nullptr) {
    DartVmEmbed_UnloadAotElf(State().app_aot_elf_handle);
    State().app_aot_elf_handle = nullptr;
    State().app_aot_elf_refcount = 0;
  }
#else
  State().app_snapshot.reset();
#endif
  State().app_isolate_snapshot_data = nullptr;
  State().app_isolate_snapshot_instructions = nullptr;
  State().app_script_uri.clear();
  dart::embedder::Cleanup();
  return true;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromKernel(
    const char* script_uri,
    const char* name,
    const uint8_t* kernel_buffer,
    intptr_t kernel_buffer_size,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (script_uri == nullptr || name == nullptr || kernel_buffer == nullptr ||
      kernel_buffer_size <= 0) {
    SetErrorIfUnset(error,
                    "DartVmEmbed_CreateIsolateFromKernel: invalid argument.");
    return nullptr;
  }

  const char* vm_flags[] = {"--no-precompilation"};
  DartVmEmbedInitConfig init_config;
  init_config.vm_flag_count = 1;
  init_config.vm_flags = vm_flags;
  if (!DartVmEmbed_Initialize(&init_config, error)) {
    return nullptr;
  }

  std::string sanitized_script_uri_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);

  DartVmEmbedState::OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    const char* effective_packages = EffectivePackagesConfig(nullptr);
    std::string sanitized_packages_config_storage;
    const char* sanitized_packages_config =
        SanitizePathLikeMain(effective_packages, &sanitized_packages_config_storage);
    group_data = new dart::bin::IsolateGroupData(
        sanitized_script_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/false);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  if (group_data->kernel_buffer() == nullptr) {
    uint8_t* copied_kernel =
        reinterpret_cast<uint8_t*>(malloc(kernel_buffer_size));
    if (copied_kernel == nullptr) {
      if (owned.owns_group) {
        delete owned.isolate_group_data;
      }
      SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromKernel: OOM.");
      return nullptr;
    }
    memcpy(copied_kernel, kernel_buffer, static_cast<size_t>(kernel_buffer_size));
    group_data->SetKernelBufferNewlyOwned(copied_kernel, kernel_buffer_size);
  }

  auto* local_isolate_data =
      reinterpret_cast<dart::bin::IsolateData*>(isolate_data);
  if (local_isolate_data == nullptr) {
    local_isolate_data = new dart::bin::IsolateData(group_data);
    owned.isolate_data = local_isolate_data;
    owned.owns_isolate = true;
  }

  Dart_IsolateFlags flags;
  Dart_IsolateFlagsInitialize(&flags);
  flags.null_safety = true;
  flags.snapshot_is_dontneed_safe = ComputeSnapshotIsDontneedSafe();
  flags.load_vmservice_library = ShouldEnableVmService();

  const uint8_t* platform_kernel_buffer = nullptr;
  intptr_t platform_kernel_buffer_size = 0;
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  dart::bin::dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
#endif
  if (platform_kernel_buffer == nullptr || platform_kernel_buffer_size == 0) {
    platform_kernel_buffer = kernel_buffer;
    platform_kernel_buffer_size = kernel_buffer_size;
  }

  Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
      sanitized_script_uri, name, platform_kernel_buffer, platform_kernel_buffer_size,
      &flags, group_data, local_isolate_data, error);
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  isolate = IsolateSetupHelper(isolate, /*is_main_isolate=*/true,
                               sanitized_script_uri,
                               /*packages_config=*/EffectivePackagesConfig(nullptr),
                               /*isolate_run_app_snapshot=*/false, &flags, error);
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  if (owned.owns_isolate || owned.owns_group) {
    State().owned_isolates[isolate] = owned;
  }
  return isolate;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromSource(
    const char* script_path,
    const char* script_uri,
    const char* name,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
  // DIFF(main_impl): this is an embedder-only convenience API.
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  (void)script_path;
  (void)script_uri;
  (void)name;
  (void)isolate_group_data;
  (void)isolate_data;
  SetErrorIfUnset(error,
                  "DartVmEmbed_CreateIsolateFromSource is unavailable in "
                  "precompiled runtime.");
  return nullptr;
#else
  if (error != nullptr) {
    *error = nullptr;
  }
  if (script_path == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromSource: script_path is null.");
    return nullptr;
  }

  const char* effective_uri = (script_uri != nullptr) ? script_uri : script_path;
  const char* effective_name = (name != nullptr) ? name : effective_uri;
  const char* effective_packages_config = EffectivePackagesConfig(nullptr);

  std::string sanitized_script_uri_storage;
  std::string sanitized_packages_config_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(effective_uri, &sanitized_script_uri_storage);
  const char* sanitized_packages_config = SanitizePathLikeMain(
      effective_packages_config, &sanitized_packages_config_storage);

  // Initialize VM first so DFE compilation infrastructure is ready.
  const char* vm_flags[] = {"--no-precompilation"};
  DartVmEmbedInitConfig init_config;
  init_config.vm_flag_count = 1;
  init_config.vm_flags = vm_flags;
  if (!DartVmEmbed_Initialize(&init_config, error)) {
    return nullptr;
  }

  uint8_t* kernel_buffer = nullptr;
  intptr_t kernel_buffer_size = 0;
  char* compile_error = nullptr;
  int compile_exit_code = 0;
  dart::bin::dfe.CompileAndReadScript(
      sanitized_script_uri, &kernel_buffer, &kernel_buffer_size, &compile_error,
      &compile_exit_code, /*package_config=*/sanitized_packages_config,
      /*for_snapshot=*/false, /*embed_sources=*/true);
  (void)compile_exit_code;
  if (kernel_buffer == nullptr || kernel_buffer_size <= 0) {
    if (compile_error != nullptr) {
      SetErrorIfUnset(error, compile_error);
      free(compile_error);
    } else {
      SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromSource: compile failed.");
    }
    return nullptr;
  }
  if (compile_error != nullptr) {
    free(compile_error);
  }

  Dart_Isolate isolate = DartVmEmbed_CreateIsolateFromKernel(
      sanitized_script_uri, effective_name, kernel_buffer, kernel_buffer_size,
      isolate_group_data, isolate_data, error);
  free(kernel_buffer);
  return isolate;
#endif
}

Dart_Isolate DartVmEmbed_CreateIsolateFromJitSnapshotFile(
    const char* snapshot_path,
    const char* script_uri,
    const char* name,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  (void)snapshot_path;
  (void)script_uri;
  (void)name;
  (void)isolate_group_data;
  (void)isolate_data;
  SetErrorIfUnset(error,
                  "DartVmEmbed_CreateIsolateFromJitSnapshotFile is unavailable in "
                  "precompiled runtime.");
  return nullptr;
#else
  if (error != nullptr) {
    *error = nullptr;
  }
  if (snapshot_path == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromJitSnapshotFile: snapshot_path is null.");
    return nullptr;
  }

  std::unique_ptr<dart::bin::AppSnapshot> app_snapshot(
      dart::bin::Snapshot::TryReadAppSnapshot(snapshot_path,
                                              /*force_load_elf_from_memory=*/false,
                                              /*decode_uri=*/true));
  if (!app_snapshot) {
    SetErrorIfUnset(error, "Failed to read app-jit snapshot.");
    return nullptr;
  }
  if (!app_snapshot->IsJIT()) {
    SetErrorIfUnset(error, "Snapshot is not a JIT app snapshot.");
    return nullptr;
  }

  const uint8_t* vm_data = nullptr;
  const uint8_t* vm_instr = nullptr;
  const uint8_t* iso_data = nullptr;
  const uint8_t* iso_instr = nullptr;
  app_snapshot->SetBuffers(&vm_data, &vm_instr, &iso_data, &iso_instr);

  // Initialize VM from the snapshot's VM pieces if needed.
  if (State().vm_initialized) {
    if (State().vm_snapshot_data != vm_data ||
        State().vm_snapshot_instructions != vm_instr) {
      SetErrorIfUnset(
          error,
          "VM already initialized with different VM snapshot pieces.");
      return nullptr;
    }
  } else {
    DartVmEmbedInitConfig init_config;
    init_config.vm_snapshot_data_override = vm_data;
    init_config.vm_snapshot_instructions_override = vm_instr;
    if (!DartVmEmbed_Initialize(&init_config, error)) {
      return nullptr;
    }
  }

  // Keep snapshot buffers alive for the lifetime of the VM.
  State().app_snapshot = std::move(app_snapshot);
  State().app_isolate_snapshot_data = iso_data;
  State().app_isolate_snapshot_instructions = iso_instr;

  const char* actual_script_uri =
      (script_uri != nullptr) ? script_uri : snapshot_path;
  const char* actual_name =
      (name != nullptr) ? name : actual_script_uri;
  return DartVmEmbed_CreateIsolateFromAppSnapshot(
      actual_script_uri, actual_name, iso_data, iso_instr, isolate_group_data,
      isolate_data, error);
#endif
}

Dart_Isolate DartVmEmbed_CreateIsolateFromAotSnapshotFile(
    const char* aot_elf_path,
    const char* script_uri,
    const char* name,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (aot_elf_path == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromAotSnapshotFile: aot_elf_path is null.");
    return nullptr;
  }
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromAotSnapshotFile is only available in AOT runtime flavor.");
  return nullptr;
#else
  DartVmEmbedAotElfHandle loaded_elf = nullptr;
  const uint8_t* vm_data = nullptr;
  const uint8_t* vm_instr = nullptr;
  const uint8_t* iso_data = nullptr;
  const uint8_t* iso_instr = nullptr;
  if (!DartVmEmbed_LoadAotElf(aot_elf_path, 0, &loaded_elf, &vm_data, &vm_instr,
                             &iso_data, &iso_instr, error)) {
    return nullptr;
  }

  if (State().vm_initialized) {
    if (State().vm_snapshot_data != vm_data ||
        State().vm_snapshot_instructions != vm_instr) {
      DartVmEmbed_UnloadAotElf(loaded_elf);
      SetErrorIfUnset(
          error,
          "VM already initialized with different VM snapshot pieces.");
      return nullptr;
    }
  } else {
    DartVmEmbedInitConfig init_config;
    init_config.start_kernel_isolate = false;
    init_config.vm_snapshot_data_override = vm_data;
    init_config.vm_snapshot_instructions_override = vm_instr;
    if (!DartVmEmbed_Initialize(&init_config, error)) {
      DartVmEmbed_UnloadAotElf(loaded_elf);
      return nullptr;
    }
  }

  // Keep the loaded app snapshot available to VM-created isolates (service
  // isolate, spawnUri) and keep the ELF alive until Cleanup().
  State().app_aot_elf_handle = loaded_elf;
  State().app_aot_elf_refcount = 0;
  State().app_isolate_snapshot_data = iso_data;
  State().app_isolate_snapshot_instructions = iso_instr;

  const char* actual_script_uri =
      (script_uri != nullptr) ? script_uri : aot_elf_path;
  const char* actual_name =
      (name != nullptr) ? name : actual_script_uri;
  Dart_Isolate isolate = DartVmEmbed_CreateIsolateFromAppSnapshot(
      actual_script_uri, actual_name, iso_data, iso_instr, isolate_group_data,
      isolate_data, error);
  if (isolate == nullptr) {
    State().app_isolate_snapshot_data = nullptr;
    State().app_isolate_snapshot_instructions = nullptr;
    State().app_aot_elf_handle = nullptr;
    State().app_aot_elf_refcount = 0;
    DartVmEmbed_UnloadAotElf(loaded_elf);
    return nullptr;
  }
  State().isolate_loaded_aot_elfs[isolate] = loaded_elf;
  State().app_aot_elf_refcount++;
  return isolate;
#endif
}

Dart_Isolate DartVmEmbed_CreateIsolateFromAppSnapshot(
    const char* script_uri,
    const char* name,
    const uint8_t* isolate_snapshot_data,
    const uint8_t* isolate_snapshot_instructions,
    void* isolate_group_data,
    void* isolate_data,
    char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (script_uri == nullptr || name == nullptr || isolate_snapshot_data == nullptr ||
      isolate_snapshot_instructions == nullptr) {
    SetErrorIfUnset(error,
                    "DartVmEmbed_CreateIsolateFromAppSnapshot: invalid argument.");
    return nullptr;
  }

  std::string sanitized_script_uri_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);

  DartVmEmbedState::OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    const char* effective_packages = EffectivePackagesConfig(nullptr);
    std::string sanitized_packages_config_storage;
    const char* sanitized_packages_config =
        SanitizePathLikeMain(effective_packages, &sanitized_packages_config_storage);
    group_data = new dart::bin::IsolateGroupData(
        sanitized_script_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/true);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  auto* local_isolate_data =
      reinterpret_cast<dart::bin::IsolateData*>(isolate_data);
  if (local_isolate_data == nullptr) {
    local_isolate_data = new dart::bin::IsolateData(group_data);
    owned.isolate_data = local_isolate_data;
    owned.owns_isolate = true;
  }

  Dart_IsolateFlags flags;
  Dart_IsolateFlagsInitialize(&flags);
  flags.null_safety = true;
  flags.snapshot_is_dontneed_safe = ComputeSnapshotIsDontneedSafe();
  flags.load_vmservice_library = ShouldEnableVmService();

  Dart_Isolate isolate = Dart_CreateIsolateGroup(
      sanitized_script_uri, name, isolate_snapshot_data,
      isolate_snapshot_instructions, &flags, group_data, local_isolate_data, error);
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  // Cache app snapshot buffers for spawnUri/service isolate compatibility.
  // DIFF(main_impl): main_impl sets these during CLI snapshot loading.
  State().app_isolate_snapshot_data = isolate_snapshot_data;
  State().app_isolate_snapshot_instructions = isolate_snapshot_instructions;

  isolate = IsolateSetupHelper(isolate, /*is_main_isolate=*/true,
                               sanitized_script_uri,
                               /*packages_config=*/EffectivePackagesConfig(nullptr),
                               /*isolate_run_app_snapshot=*/true, &flags, error);
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  if (owned.owns_isolate || owned.owns_group) {
    State().owned_isolates[isolate] = owned;
  }
  return isolate;
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
      out_vm_snapshot_instructions == nullptr || out_isolate_snapshot_data == nullptr ||
      out_isolate_snapshot_instructions == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_LoadAotElf: invalid argument.");
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
      *error = DupMessage(load_error != nullptr ? load_error : "Dart_LoadELF failed.");
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
  SetErrorIfUnset(error, "DartVmEmbed_LoadAotElf is only available in AOT runtime flavor.");
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
    SetErrorIfUnset(error, "DartVmEmbed_RunRootEntryOnIsolate: isolate is null.");
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

bool DartVmEmbed_SetFileModifiedCallback(DartVmEmbedFileModifiedCallback callback,
                                        char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  State().file_modified_callback = callback;
  return true;
}

bool DartVmEmbed_IsReloading(void) {
  if (Dart_CurrentIsolate() == nullptr) {
    return false;
  }
  return Dart_IsReloading();
}

bool DartVmEmbed_HasServiceMessages(void) {
  if (Dart_CurrentIsolate() == nullptr) {
    return false;
  }
  return Dart_HasServiceMessages();
}

bool DartVmEmbed_HandleServiceMessages(void) {
  if (Dart_CurrentIsolate() == nullptr) {
    return false;
  }
  return Dart_HandleServiceMessages();
}

Dart_Handle DartVmEmbed_RunLoop(void) {
  return Dart_RunLoop();
}

void DartVmEmbed_ShutdownIsolate(void) {
  Dart_Isolate isolate = Dart_CurrentIsolate();
  if (isolate == nullptr) {
    return;
  }

  Dart_ShutdownIsolate();

  auto aot_it = State().isolate_loaded_aot_elfs.find(isolate);
  if (aot_it != State().isolate_loaded_aot_elfs.end()) {
    // DIFF(main_impl): main_impl keeps the main app snapshot backing store
    // alive for process lifetime. Here we keep it alive until Cleanup(), so we
    // don't unload the shared handle when isolates shut down.
    if (aot_it->second == State().app_aot_elf_handle) {
      if (State().app_aot_elf_refcount > 0) {
        State().app_aot_elf_refcount--;
      } else {
        DartVmEmbed_UnloadAotElf(aot_it->second);
      }
    } 
    State().isolate_loaded_aot_elfs.erase(aot_it);
  }

  State().isolate_kernel_buffers.erase(isolate);

  auto owned_it = State().owned_isolates.find(isolate);
  if (owned_it != State().owned_isolates.end()) {
    if (owned_it->second.owns_isolate) {
      delete owned_it->second.isolate_data;
    }
    if (owned_it->second.owns_group) {
      delete owned_it->second.isolate_group_data;
    }
    State().owned_isolates.erase(owned_it);
  }
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
