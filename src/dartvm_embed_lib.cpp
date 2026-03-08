#include "dartvm_embed_lib.h"

#include <assert.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <bin/builtin.h>
#include <bin/dfe.h>
#include <bin/dartutils.h>
#include <bin/file.h>
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
#include <bin/elf_loader.h>
#endif
#include <bin/isolate_data.h>
#include <bin/loader.h>
#include <bin/main_options.h>
#include <bin/vmservice_impl.h>
#include <include/dart_api.h>
#include <include/dart_embedder_api.h>
#include <include/dart_tools_api.h>

static bool g_vm_initialized = false;
static std::unordered_map<Dart_Isolate, DartVmEmbedAotElfHandle>
    g_isolate_loaded_aot_elfs;
static std::unordered_map<Dart_Isolate, std::vector<uint8_t>>
    g_isolate_kernel_buffers;

struct OwnedIsolateState {
  dart::bin::IsolateGroupData* isolate_group_data = nullptr;
  dart::bin::IsolateData* isolate_data = nullptr;
  bool owns_group = false;
  bool owns_isolate = false;
};

static std::unordered_map<Dart_Isolate, OwnedIsolateState> g_owned_isolates;
static std::unordered_set<dart::bin::IsolateData*> g_callback_owned_isolate_data;
static std::unordered_set<dart::bin::IsolateGroupData*> g_callback_owned_group_data;
static std::unordered_set<Dart_Isolate> g_vmservice_warmed_isolates;
static DartVmEmbedFileModifiedCallback g_file_modified_callback = nullptr;
static std::string g_vm_service_ip = "127.0.0.1";
static int g_vm_service_port = 8181;
static bool g_vm_service_auth_codes_disabled = true;

static bool FileModifiedCallbackTrampoline(const char* url, int64_t since) {
  if (g_file_modified_callback != nullptr) {
    return g_file_modified_callback(url, since);
  }
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

static bool ServiceStreamListenCallback(const char* stream_id) {
  (void)stream_id;
  return true;
}

static void ServiceStreamCancelCallback(const char* stream_id) {
  (void)stream_id;
}

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

static const char* EffectivePackagesConfig(const char* packages_config) {
  if (packages_config != nullptr) {
    return packages_config;
  }
  return dart::bin::Options::packages_file();
}

static bool ShouldEnableVmService(void) {
  const char* hot_reload = getenv("DARTVM_EMBED_HOT_RELOAD");
  return hot_reload != nullptr && strcmp(hot_reload, "1") == 0;
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

static bool IsVmServiceResponseSuccess(const uint8_t* response_json,
                                       intptr_t response_len) {
  if (response_json == nullptr || response_len <= 0) {
    return false;
  }
  std::string response(reinterpret_cast<const char*>(response_json),
                       static_cast<size_t>(response_len));
  if (response.find("\"error\"") != std::string::npos &&
      response.find("\"error\":null") == std::string::npos) {
    return false;
  }
  return response.find("\"result\"") != std::string::npos;
}

static bool WarmupVmServiceReloadCompiler(Dart_Isolate isolate, char** error) {
  if (!ShouldEnableVmService()) {
    return true;
  }
  if (g_vmservice_warmed_isolates.find(isolate) != g_vmservice_warmed_isolates.end()) {
    return true;
  }

  bool entered = false;
  if (Dart_CurrentIsolate() == nullptr) {
    Dart_EnterIsolate(isolate);
    entered = true;
  }
  const char* isolate_service_id = Dart_IsolateServiceId(isolate);
  std::string service_id =
      isolate_service_id != nullptr ? isolate_service_id : "";
  if (entered) {
    Dart_ExitIsolate();
  }
  if (service_id.empty()) {
    SetErrorIfUnset(error,
                    "WarmupVmServiceReloadCompiler: failed to resolve isolate "
                    "service id.");
    return false;
  }

  const int max_retries = 10;
  for (int i = 0; i < max_retries; ++i) {
    const std::string req =
        "{\"jsonrpc\":\"2.0\",\"id\":\"warmup\",\"method\":\"reloadSources\","
        "\"params\":{\"isolateId\":\"" +
        service_id + "\",\"force\":true}}";
    uint8_t* response_json = nullptr;
    intptr_t response_len = 0;
    char* vm_error = nullptr;
    const bool invoked = Dart_InvokeVMServiceMethod(
        reinterpret_cast<uint8_t*>(const_cast<char*>(req.c_str())),
        static_cast<intptr_t>(req.size()), &response_json, &response_len, &vm_error);
    if (invoked && IsVmServiceResponseSuccess(response_json, response_len)) {
      free(response_json);
      free(vm_error);
      g_vmservice_warmed_isolates.insert(isolate);
      return true;
    }
    free(response_json);
    free(vm_error);
    usleep(200 * 1000);
  }

  SetErrorIfUnset(error,
                  "WarmupVmServiceReloadCompiler: reloadSources warmup failed.");
  return false;
}

static Dart_Handle SetupCoreLibraries(Dart_Isolate isolate,
                                      dart::bin::IsolateData* isolate_data,
                                      bool is_isolate_group_start,
                                      bool is_kernel_isolate,
                                      const char** resolved_packages_config) {
  (void)isolate;
  auto* isolate_group_data = isolate_data->isolate_group_data();
  const char* packages_file = isolate_data->packages_file();
  const char* script_uri = isolate_group_data->script_url;

  Dart_Handle result =
      dart::bin::DartUtils::PrepareForScriptLoading(false, false);
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

  const char* namespc = is_kernel_isolate ? nullptr : nullptr;
  result = dart::bin::DartUtils::SetupIOLibrary(namespc, script_uri, false);
  if (Dart_IsError(result)) {
    return result;
  }

  return Dart_Null();
}

static bool SetupRootIsolateAndMakeRunnable(Dart_Isolate isolate,
                                            const char* script_uri,
                                            bool isolate_run_app_snapshot,
                                            char** error) {
  Dart_EnterScope();

  Dart_Handle result =
      Dart_SetLibraryTagHandler(dart::bin::Loader::LibraryTagHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return false;
  }

  result = Dart_SetDeferredLoadHandler(dart::bin::Loader::DeferredLoadHandler);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return false;
  }

  auto* isolate_data =
      reinterpret_cast<dart::bin::IsolateData*>(Dart_IsolateData(isolate));
  if (isolate_data == nullptr) {
    Dart_ExitScope();
    SetErrorIfUnset(error, "SetupRootIsolateAndMakeRunnable: isolate_data is null.");
    Dart_ShutdownIsolate();
    return false;
  }

  const char* resolved_packages_config = nullptr;
  result = SetupCoreLibraries(isolate, isolate_data,
                              /*is_isolate_group_start=*/true,
                              /*is_kernel_isolate=*/false,
                              &resolved_packages_config);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    Dart_ShutdownIsolate();
    return false;
  }

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  auto* isolate_group_data = isolate_data->isolate_group_data();
  const uint8_t* kernel_buffer = isolate_group_data->kernel_buffer().get();
  const intptr_t kernel_buffer_size = isolate_group_data->kernel_buffer_size();

  if (!isolate_run_app_snapshot && kernel_buffer != nullptr) {
    Dart_Handle uri =
        dart::bin::DartUtils::ResolveScript(Dart_NewStringFromCString(script_uri));
    if (SetErrorFromHandle(uri, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return false;
    }

    result = Dart_LoadScriptFromKernel(kernel_buffer, kernel_buffer_size);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return false;
    }

    const char* resolved_script_uri = nullptr;
    result = Dart_StringToCString(uri, &resolved_script_uri);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return false;
    }

    result = dart::bin::Loader::InitForSnapshot(resolved_script_uri, isolate_data);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return false;
    }
  }
#endif

  if (isolate_run_app_snapshot) {
    result = dart::bin::Loader::InitForSnapshot(script_uri, isolate_data);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      return false;
    }
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
    Dart_EnterIsolate(isolate);
    Dart_ShutdownIsolate();
    return false;
  }

  return true;
}

static bool OnIsolateInitialize(void** child_callback_data, char** error) {
  if (child_callback_data != nullptr) {
    *child_callback_data = nullptr;
  }
  if (error != nullptr) {
    *error = nullptr;
  }

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
      Dart_CurrentIsolate(), isolate_data,
      /*is_isolate_group_start=*/false,
      /*is_kernel_isolate=*/false,
      /*resolved_packages_config=*/nullptr);
  if (SetErrorFromHandle(result, error)) {
    Dart_ExitScope();
    if (child_callback_data != nullptr) {
      *child_callback_data = nullptr;
    }
    delete isolate_data;
    return false;
  }

  if (isolate_group_data->RunFromAppSnapshot()) {
    result = dart::bin::Loader::InitForSnapshot(script_uri, isolate_data);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      if (child_callback_data != nullptr) {
        *child_callback_data = nullptr;
      }
      delete isolate_data;
      return false;
    }
  } else {
    result = dart::bin::DartUtils::ResolveScript(Dart_NewStringFromCString(script_uri));
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      if (child_callback_data != nullptr) {
        *child_callback_data = nullptr;
      }
      delete isolate_data;
      return false;
    }

    if (isolate_group_data->kernel_buffer() != nullptr) {
      const char* resolved_script_uri = nullptr;
      result = Dart_StringToCString(result, &resolved_script_uri);
      if (SetErrorFromHandle(result, error)) {
        Dart_ExitScope();
        if (child_callback_data != nullptr) {
          *child_callback_data = nullptr;
        }
        delete isolate_data;
        return false;
      }

      result = dart::bin::Loader::InitForSnapshot(resolved_script_uri, isolate_data);
      if (SetErrorFromHandle(result, error)) {
        Dart_ExitScope();
        if (child_callback_data != nullptr) {
          *child_callback_data = nullptr;
        }
        delete isolate_data;
        return false;
      }
    }
  }

  Dart_ExitScope();
  g_callback_owned_isolate_data.insert(isolate_data);
  return true;
}

static void OnIsolateShutdown(void* isolate_group_data, void* isolate_data) {
  (void)isolate_group_data;
  (void)isolate_data;
  Dart_EnterScope();
  Dart_Handle sticky_error = Dart_GetStickyError();
  if (!Dart_IsNull(sticky_error) && !Dart_IsFatalError(sticky_error)) {
    fprintf(stderr, "%s\n", Dart_GetError(sticky_error));
  }
  Dart_ExitScope();
}

static void CleanupIsolate(void* isolate_group_data, void* callback_data) {
  (void)isolate_group_data;
  auto* isolate_data = reinterpret_cast<dart::bin::IsolateData*>(callback_data);
  if (isolate_data == nullptr) {
    return;
  }
  if (g_callback_owned_isolate_data.erase(isolate_data) > 0) {
    delete isolate_data;
  }
}

static void CleanupGroup(void* callback_data) {
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(callback_data);
  if (group_data == nullptr) {
    return;
  }
  if (g_callback_owned_group_data.erase(group_data) > 0) {
    delete group_data;
  }
}

static Dart_Isolate OnCreateIsolateGroup(const char* script_uri,
                                         const char* main,
                                         const char* package_root,
                                         const char* package_config,
                                         Dart_IsolateFlags* flags,
                                         void* isolate_data,
                                         char** error) {
  (void)isolate_data;
  (void)package_root;
  if (error != nullptr) {
    *error = nullptr;
  }

  if (script_uri == nullptr) {
    SetErrorIfUnset(error, "OnCreateIsolateGroup: script_uri is null.");
    return nullptr;
  }
  if (flags == nullptr) {
    SetErrorIfUnset(error, "OnCreateIsolateGroup: flags is null.");
    return nullptr;
  }

  const char* effective_name = (main != nullptr) ? main : script_uri;
  const char* effective_packages_config = EffectivePackagesConfig(package_config);
  std::string sanitized_script_uri_storage;
  std::string sanitized_packages_config_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);
  const char* sanitized_packages_config = SanitizePathLikeMain(
      effective_packages_config, &sanitized_packages_config_storage);

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  if (strcmp(script_uri, DART_KERNEL_ISOLATE_NAME) == 0) {
    const char* kernel_snapshot_uri = dart::bin::dfe.frontend_filename();
    const char* uri =
        (kernel_snapshot_uri != nullptr) ? kernel_snapshot_uri : script_uri;
    std::string sanitized_kernel_uri_storage;
    const char* sanitized_kernel_uri =
        SanitizePathLikeMain(uri, &sanitized_kernel_uri_storage);

    const uint8_t* kernel_service_buffer = nullptr;
    intptr_t kernel_service_buffer_size = 0;
    dart::bin::dfe.LoadKernelService(&kernel_service_buffer,
                                     &kernel_service_buffer_size);
    if (kernel_service_buffer == nullptr || kernel_service_buffer_size <= 0) {
      SetErrorIfUnset(error,
                      "OnCreateIsolateGroup: failed to load kernel-service "
                      "kernel binary.");
      return nullptr;
    }

    auto* group_data = new dart::bin::IsolateGroupData(
        sanitized_kernel_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/false);
    group_data->SetKernelBufferUnowned(const_cast<uint8_t*>(kernel_service_buffer),
                                       kernel_service_buffer_size);
    auto* child_isolate_data = new dart::bin::IsolateData(group_data);
    Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
        DART_KERNEL_ISOLATE_NAME, DART_KERNEL_ISOLATE_NAME, kernel_service_buffer,
        kernel_service_buffer_size, flags, group_data, child_isolate_data, error);
    if (isolate == nullptr) {
      delete child_isolate_data;
      delete group_data;
      return nullptr;
    }
    if (!SetupRootIsolateAndMakeRunnable(isolate, sanitized_kernel_uri,
                                         /*isolate_run_app_snapshot=*/false,
                                         error)) {
      return nullptr;
    }
    g_callback_owned_isolate_data.insert(child_isolate_data);
    g_callback_owned_group_data.insert(group_data);
    return isolate;
  }
#endif

  if (strcmp(script_uri, DART_VM_SERVICE_ISOLATE_NAME) == 0) {
    flags->load_vmservice_library = true;
    flags->null_safety = true;
    flags->is_service_isolate = true;

    auto* group_data = new dart::bin::IsolateGroupData(
        sanitized_script_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/false);

    Dart_Isolate isolate = Dart_CreateIsolateGroup(
        sanitized_script_uri, DART_VM_SERVICE_ISOLATE_NAME,
        kDartCoreIsolateSnapshotData, kDartCoreIsolateSnapshotInstructions, flags,
        group_data, /*isolate_data=*/nullptr, error);
    if (isolate == nullptr) {
      delete group_data;
      return nullptr;
    }

    Dart_EnterScope();
    Dart_Handle result =
        Dart_SetLibraryTagHandler(dart::bin::Loader::LibraryTagHandler);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      delete group_data;
      return nullptr;
    }
    result = Dart_SetDeferredLoadHandler(dart::bin::Loader::DeferredLoadHandler);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      delete group_data;
      return nullptr;
    }

    if (!dart::bin::VmService::Setup(
            g_vm_service_ip.c_str(), g_vm_service_port,
            /*dev_mode_server=*/false, g_vm_service_auth_codes_disabled,
            /*write_service_info_filename=*/nullptr,
            /*trace_loading=*/false,
            /*deterministic=*/false,
            /*enable_service_port_fallback=*/true,
            /*wait_for_dds_to_advertise_service=*/false,
            /*serve_devtools=*/true,
            /*serve_observatory=*/true,
            /*print_dtd=*/false,
            /*should_use_resident_compiler=*/false,
            /*resident_compiler_info_file_path=*/nullptr)) {
      SetErrorIfUnset(error, dart::bin::VmService::GetErrorMessage());
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      delete group_data;
      return nullptr;
    }
    result = Dart_SetEnvironmentCallback(dart::bin::DartUtils::EnvironmentCallback);
    if (SetErrorFromHandle(result, error)) {
      Dart_ExitScope();
      Dart_ShutdownIsolate();
      delete group_data;
      return nullptr;
    }

    Dart_ExitScope();
    Dart_ExitIsolate();
    g_callback_owned_group_data.insert(group_data);
    return isolate;
  }

  auto* group_data = new dart::bin::IsolateGroupData(
      sanitized_script_uri, sanitized_packages_config, nullptr,
      /*isolate_run_app_snapshot=*/false);
  auto* child_isolate_data = new dart::bin::IsolateData(group_data);

  Dart_Isolate isolate = nullptr;
#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  uint8_t* kernel_buffer = nullptr;
  intptr_t kernel_buffer_size = 0;
  char* compile_error = nullptr;
  int compile_exit_code = 0;
  dart::bin::dfe.CompileAndReadScript(
      sanitized_script_uri, &kernel_buffer, &kernel_buffer_size, &compile_error,
      &compile_exit_code,
      /*package_config=*/sanitized_packages_config, /*for_snapshot=*/false,
      /*embed_sources=*/true);
  (void)compile_exit_code;
  if (kernel_buffer == nullptr || kernel_buffer_size <= 0) {
    if (compile_error != nullptr) {
      SetErrorIfUnset(error, compile_error);
      free(compile_error);
    }
    delete child_isolate_data;
    delete group_data;
    SetErrorIfUnset(error, "OnCreateIsolateGroup: failed to compile script.");
    return nullptr;
  }
  if (compile_error != nullptr) {
    free(compile_error);
  }
  group_data->SetKernelBufferNewlyOwned(kernel_buffer, kernel_buffer_size);

  const uint8_t* platform_kernel_buffer = nullptr;
  intptr_t platform_kernel_buffer_size = 0;
  dart::bin::dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
  if (platform_kernel_buffer == nullptr || platform_kernel_buffer_size == 0) {
    platform_kernel_buffer = kernel_buffer;
    platform_kernel_buffer_size = kernel_buffer_size;
  }

  isolate = Dart_CreateIsolateGroupFromKernel(
      sanitized_script_uri, effective_name, platform_kernel_buffer,
      platform_kernel_buffer_size, flags, group_data, child_isolate_data, error);
#else
  isolate = Dart_CreateIsolateGroup(
      sanitized_script_uri, effective_name, kDartCoreIsolateSnapshotData,
      kDartCoreIsolateSnapshotInstructions, flags, group_data, child_isolate_data,
      error);
#endif

  if (isolate == nullptr) {
    delete child_isolate_data;
    delete group_data;
    return nullptr;
  }
  if (!SetupRootIsolateAndMakeRunnable(isolate, sanitized_script_uri,
                                       /*isolate_run_app_snapshot=*/false,
                                       error)) {
    return nullptr;
  }

  g_callback_owned_isolate_data.insert(child_isolate_data);
  g_callback_owned_group_data.insert(group_data);
  return isolate;
}

extern "C" {

bool DartVmEmbed_Initialize(const DartVmEmbedInitConfig* config, char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }

  if (g_vm_initialized) {
    return true;
  }

  if (const char* ip = getenv("DARTVM_EMBED_VM_SERVICE_IP")) {
    if (ip[0] != '\0') {
      g_vm_service_ip = ip;
    }
  }
  if (const char* port = getenv("DARTVM_EMBED_VM_SERVICE_PORT")) {
    const int value = atoi(port);
    if (value > 0) {
      g_vm_service_port = value;
    }
  }
  if (const char* auth = getenv("DARTVM_EMBED_VM_SERVICE_AUTH_CODES_DISABLED")) {
    g_vm_service_auth_codes_disabled = (strcmp(auth, "0") != 0);
  }

  char* embedder_error = nullptr;
  if (!dart::embedder::InitOnce(&embedder_error)) {
    if (error != nullptr) {
      *error = DupMessage(embedder_error);
    }
    free(embedder_error);
    return false;
  }
  dart::bin::Loader::InitOnce();

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

#if !defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  // Keep ordering aligned with runtime/bin/main_impl.cc: DFE platform loading
  // happens only after VM flags are parsed.
  dart::bin::dfe.Init();
  dart::bin::dfe.set_use_dfe();
  dart::bin::dfe.set_use_incremental_compiler(true);
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
#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  params.start_kernel_isolate = false;
#else
  params.start_kernel_isolate =
      (config == nullptr) ? true : config->start_kernel_isolate;
#endif
  params.initialize_isolate = OnIsolateInitialize;
  params.create_group = OnCreateIsolateGroup;
  params.shutdown_isolate = OnIsolateShutdown;
  params.cleanup_isolate = CleanupIsolate;
  params.cleanup_group = CleanupGroup;
  params.file_open = dart::bin::DartUtils::OpenFile;
  params.file_read = dart::bin::DartUtils::ReadFile;
  params.file_write = dart::bin::DartUtils::WriteFile;
  params.file_close = dart::bin::DartUtils::CloseFile;
  params.entropy_source = dart::bin::DartUtils::EntropySource;

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
  dart::embedder::Cleanup();
  return true;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromKernel(const char* script_uri,
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
  std::string sanitized_script_uri_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);

  OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    const char* effective_packages = EffectivePackagesConfig(nullptr);
    std::string sanitized_packages_config_storage;
    const char* sanitized_packages_config = SanitizePathLikeMain(
        effective_packages, &sanitized_packages_config_storage);
    group_data = new dart::bin::IsolateGroupData(
        sanitized_script_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/false);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  if (group_data->kernel_buffer() == nullptr) {
    uint8_t* copied_kernel = reinterpret_cast<uint8_t*>(malloc(kernel_buffer_size));
    if (copied_kernel == nullptr) {
      if (owned.owns_group) {
        delete owned.isolate_group_data;
      }
      SetErrorIfUnset(error,
                      "DartVmEmbed_CreateIsolateFromKernel: OOM while copying kernel.");
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
  flags.snapshot_is_dontneed_safe = false;
  flags.load_vmservice_library = ShouldEnableVmService();

  void* actual_group_data =
      isolate_group_data != nullptr ? isolate_group_data : owned.isolate_group_data;
  void* actual_isolate_data =
      isolate_data != nullptr ? isolate_data : owned.isolate_data;

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
      &flags, actual_group_data, actual_isolate_data, error);
  if (isolate == nullptr) {
    SetErrorIfUnset(error,
                    "Dart_CreateIsolateGroupFromKernel returned null.");
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  if (!SetupRootIsolateAndMakeRunnable(isolate, sanitized_script_uri,
                                       /*isolate_run_app_snapshot=*/false,
                                       error)) {
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
  if (error != nullptr) {
    *error = nullptr;
  }
  if (script_uri == nullptr || name == nullptr || isolate_snapshot_data == nullptr ||
      isolate_snapshot_instructions == nullptr) {
    SetErrorIfUnset(
        error, "DartVmEmbed_CreateIsolateFromAppSnapshot: invalid argument.");
    return nullptr;
  }
  std::string sanitized_script_uri_storage;
  const char* sanitized_script_uri =
      SanitizePathLikeMain(script_uri, &sanitized_script_uri_storage);

  OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    const char* effective_packages = EffectivePackagesConfig(nullptr);
    std::string sanitized_packages_config_storage;
    const char* sanitized_packages_config = SanitizePathLikeMain(
        effective_packages, &sanitized_packages_config_storage);
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
  flags.snapshot_is_dontneed_safe = false;
  flags.load_vmservice_library = ShouldEnableVmService();

  void* actual_group_data =
      isolate_group_data != nullptr ? isolate_group_data : owned.isolate_group_data;
  void* actual_isolate_data =
      isolate_data != nullptr ? isolate_data : owned.isolate_data;

  Dart_Isolate isolate = Dart_CreateIsolateGroup(
      sanitized_script_uri, name, isolate_snapshot_data,
      isolate_snapshot_instructions,
      &flags, actual_group_data, actual_isolate_data, error);
  if (isolate == nullptr) {
    SetErrorIfUnset(error, "Dart_CreateIsolateGroup returned null.");
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    return nullptr;
  }

  if (!SetupRootIsolateAndMakeRunnable(isolate, sanitized_script_uri,
                                       /*isolate_run_app_snapshot=*/true,
                                       error)) {
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

  return isolate;
}

Dart_Isolate DartVmEmbed_CreateIsolateFromSource(const char* script_path,
                                                 const char* script_uri,
                                                 const char* name,
                                                 void* isolate_group_data,
                                                 void* isolate_data,
                                                 char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
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
  if (script_path == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_CreateIsolateFromSource: script_path is null.");
    return nullptr;
  }
  const char* vm_flags[] = {"--no-precompilation"};
  DartVmEmbedInitConfig config;
  config.vm_flag_count = 1;
  config.vm_flags = vm_flags;
  if (!DartVmEmbed_Initialize(&config, error)) {
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

  uint8_t* kernel_buffer = nullptr;
  intptr_t kernel_buffer_size = 0;
  char* compile_error = nullptr;
  int compile_exit_code = 0;
  dart::bin::dfe.CompileAndReadScript(
      sanitized_script_uri, &kernel_buffer, &kernel_buffer_size, &compile_error,
      &compile_exit_code,
      /*package_config=*/sanitized_packages_config, /*for_snapshot=*/false,
      /*embed_sources=*/true);
  (void)compile_exit_code;
  if (kernel_buffer == nullptr || kernel_buffer_size <= 0) {
    if (compile_error != nullptr) {
      SetErrorIfUnset(error, compile_error);
      free(compile_error);
    }
    SetErrorIfUnset(error,
                    "DartVmEmbed_CreateIsolateFromSource: failed to compile "
                    "source to kernel.");
    return nullptr;
  }
  if (compile_error != nullptr) {
    free(compile_error);
  }

  OwnedIsolateState owned{};
  auto* group_data =
      reinterpret_cast<dart::bin::IsolateGroupData*>(isolate_group_data);
  if (group_data == nullptr) {
    group_data = new dart::bin::IsolateGroupData(
        sanitized_script_uri, sanitized_packages_config, nullptr,
        /*isolate_run_app_snapshot=*/false);
    owned.isolate_group_data = group_data;
    owned.owns_group = true;
  }

  if (group_data->kernel_buffer() == nullptr) {
    uint8_t* copied_kernel = reinterpret_cast<uint8_t*>(malloc(kernel_buffer_size));
    if (copied_kernel == nullptr) {
      free(kernel_buffer);
      if (owned.owns_group) {
        delete owned.isolate_group_data;
      }
      SetErrorIfUnset(error,
                      "DartVmEmbed_CreateIsolateFromSource: OOM while copying kernel.");
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
  flags.snapshot_is_dontneed_safe = false;
  flags.load_vmservice_library = ShouldEnableVmService();

  const uint8_t* platform_kernel_buffer = nullptr;
  intptr_t platform_kernel_buffer_size = 0;
  dart::bin::dfe.LoadPlatform(&platform_kernel_buffer, &platform_kernel_buffer_size);
  if (platform_kernel_buffer == nullptr || platform_kernel_buffer_size == 0) {
    platform_kernel_buffer = kernel_buffer;
    platform_kernel_buffer_size = kernel_buffer_size;
  }

  void* actual_group_data =
      isolate_group_data != nullptr ? isolate_group_data : owned.isolate_group_data;
  void* actual_isolate_data =
      isolate_data != nullptr ? isolate_data : owned.isolate_data;

  Dart_Isolate isolate = Dart_CreateIsolateGroupFromKernel(
      sanitized_script_uri, effective_name, platform_kernel_buffer,
      platform_kernel_buffer_size, &flags, actual_group_data, actual_isolate_data,
      error);
  free(kernel_buffer);
  if (isolate == nullptr) {
    if (owned.owns_isolate) {
      delete owned.isolate_data;
    }
    if (owned.owns_group) {
      delete owned.isolate_group_data;
    }
    SetErrorIfUnset(error,
                    "DartVmEmbed_CreateIsolateFromSource: "
                    "Dart_CreateIsolateGroupFromKernel returned null.");
    return nullptr;
  }

  if (!SetupRootIsolateAndMakeRunnable(isolate, sanitized_script_uri,
                                       /*isolate_run_app_snapshot=*/false,
                                       error)) {
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
  return isolate;
#endif
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
    SetErrorIfUnset(
        error,
        "DartVmEmbed_CreateIsolateFromProgramFile: program_path is null.");
    return nullptr;
  }

  const char* actual_script_uri = script_uri != nullptr ? script_uri : program_path;
  const char* isolate_name = "isolate";

#if defined(DARTVM_EMBED_DEFAULT_PRECOMPILATION_FLAG)
  DartVmEmbedAotElfHandle loaded_elf = nullptr;
  const uint8_t* vm_data = nullptr;
  const uint8_t* vm_instr = nullptr;
  const uint8_t* iso_data = nullptr;
  const uint8_t* iso_instr = nullptr;
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
    SetErrorIfUnset(
        error,
        "DartVmEmbed_LoadAotElf: output pointers must not be null.");
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
  SetErrorIfUnset(
      error,
      "DartVmEmbed_LoadAotElf is only available in AOT runtime flavor.");
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
    SetErrorIfUnset(error,
                    "DartVmEmbed_RunRootEntryOnIsolate: isolate is null.");
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
  if (!ok) {
    return false;
  }
  if (!WarmupVmServiceReloadCompiler(isolate, error)) {
    return false;
  }
  return true;
}

bool DartVmEmbed_SetFileModifiedCallback(DartVmEmbedFileModifiedCallback callback,
                                         char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  g_file_modified_callback = callback;
  // The VM callback is installed once during initialization and forwards to
  // g_file_modified_callback, so updates only mutate the function pointer.
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

bool DartVmEmbed_RunLoopOnIsolate(Dart_Isolate isolate, char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (isolate == nullptr) {
    SetErrorIfUnset(error, "DartVmEmbed_RunLoopOnIsolate: isolate is null.");
    return false;
  }

  bool entered_isolate = false;
  if (Dart_CurrentIsolate() == nullptr) {
    Dart_EnterIsolate(isolate);
    entered_isolate = true;
  }

  Dart_EnterScope();
  Dart_Handle result = Dart_RunLoop();
  Dart_ExitScope();

  if (entered_isolate) {
    Dart_ExitIsolate();
  }

  if (Dart_IsError(result)) {
    SetErrorIfUnset(error, Dart_GetError(result));
    return false;
  }
  return true;
}

void DartVmEmbed_ShutdownIsolate(void) {
  Dart_Isolate isolate = Dart_CurrentIsolate();
  if (isolate != nullptr) {
    Dart_ShutdownIsolate();
  }

  if (isolate != nullptr) {
    g_vmservice_warmed_isolates.erase(isolate);

    auto aot_it = g_isolate_loaded_aot_elfs.find(isolate);
    if (aot_it != g_isolate_loaded_aot_elfs.end()) {
      DartVmEmbed_UnloadAotElf(aot_it->second);
      g_isolate_loaded_aot_elfs.erase(aot_it);
    }

    g_isolate_kernel_buffers.erase(isolate);

    auto owned_it = g_owned_isolates.find(isolate);
    if (owned_it != g_owned_isolates.end()) {
      if (owned_it->second.owns_isolate) {
        delete owned_it->second.isolate_data;
      }
      if (owned_it->second.owns_group) {
        delete owned_it->second.isolate_group_data;
      }
      g_owned_isolates.erase(owned_it);
    }

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
